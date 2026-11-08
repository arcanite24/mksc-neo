#include "course_3d.h"
#include "mesh_course_fixture.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if(!(x)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);return 1;} } while(0)
#define TAG(s) PORTABLE_COURSE_3D_TAG((s)[0],(s)[1],(s)[2],(s)[3])
enum { COURSE_ID=70000u, TABLES=15u };
typedef struct {
    uint8_t storage[4097],catalog[40],texture[36];
    uint32_t offsets[TABLES];
    size_t size;
    PortableAssetSectionView sections[7];
    PortableAssetPackView pack;
} Fixture;
static void put(uint8_t *p,uint32_t n)
{ for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(n>>(8u*i)); }
static uint32_t get(const uint8_t *p)
{ return (uint32_t)p[0]|((uint32_t)p[1]<<8u)|((uint32_t)p[2]<<16u)|((uint32_t)p[3]<<24u); }
static uint8_t *data(Fixture *f){return f->storage+1u;}
static uint8_t *table(Fixture *f,unsigned index){return data(f)+f->offsets[index];}
static int append_row(Fixture *f,unsigned index,const uint8_t *source)
{
    uint8_t *entry=data(f)+64u+index*20u;
    const uint32_t count=get(entry+8u),stride=get(entry+12u);
    const size_t end=f->offsets[index]+(size_t)count*stride;
    uint8_t copy[128]={0};
    if(stride>sizeof copy || f->size+stride>sizeof f->storage-1u)return 0;
    if(source)memcpy(copy,source,stride);
    memmove(data(f)+end+stride,data(f)+end,f->size-end);
    memcpy(data(f)+end,copy,stride);
    for(unsigned i=0u;i<TABLES;++i)if(i!=index && f->offsets[i]>=end){
        f->offsets[i]+=stride;put(data(f)+64u+i*20u+4u,f->offsets[i]);}
    put(entry+8u,count+1u);f->size+=stride;put(data(f)+8u,(uint32_t)f->size);
    f->sections[0].size=f->size;
    return 1;
}
static void init(Fixture *f)
{
    static const char *const names[TABLES]={"STRS","META","VERT","TRIS","MATS","COLV","COLT","SURF","CPNT","CPED","RTHD","RTND","RTED","GRID","PROP"};
    static const uint32_t stride[TABLES]={1,128,32,16,48,12,32,48,64,4,32,32,4,32,32};
    static const uint32_t count[TABLES]={25,1,3,1,1,3,1,1,1,0,1,2,1,1,1};
    memset(f,0,sizeof *f);uint8_t *b=data(f);size_t at=64u+TABLES*20u;
    put(b,TAG("C3D1"));put(b+4u,1);put(b+12u,COURSE_ID);put(b+20u,64);put(b+24u,TABLES);
    for(unsigned i=0;i<TABLES;++i){uint8_t *p=b+64u+i*20u;f->offsets[i]=(uint32_t)at;
        put(p,TAG(names[i]));put(p+4u,(uint32_t)at);put(p+8u,count[i]);put(p+12u,stride[i]);put(p+16u,i<14u?1u:0u);
        at+=count[i]*stride[i];at=(at+3u)&~(size_t)3u;}
    f->size=at;put(b+8u,(uint32_t)at);
    memcpy(table(f,0),"Test\0Author\0Cup\0demo:key\0",25u);
    uint8_t *p=table(f,1);put(p+4u,5);put(p+8u,12);put(p+12u,2);put(p+16u,3);put(p+20u,17);
    for(unsigned i=7;i<=11;++i)put(p+i*4u,UINT32_MAX);
    put(p+48u,0x123456ff);put(p+60u,100u*65536u);put(p+76u,1u<<30u);put(p+84u,10u*65536u);
    put(p+100u,1);
    for(unsigned i=0;i<3;++i){p=table(f,2)+i*32u;put(p,(uint32_t)(i==1?65536:0));
        put(p+4u,(uint32_t)(i==2?65536:0));put(p+20u,1u<<30u);
        memcpy(table(f,5)+i*12u,p,12u);}
    p=table(f,3);put(p+4u,1);put(p+8u,2);
    p=table(f,4);for(unsigned i=0;i<4;++i)put(p+i*4u,UINT32_MAX);
    put(p+16u,0xabcdef80);put(p+28u,65536);put(p+32u,32768);
    p=table(f,6);put(p+4u,1);put(p+8u,2);for(unsigned i=5;i<8;++i)put(p+i*4u,UINT32_MAX);
    p=table(f,7);put(p+8u,65536);put(p+12u,65536);put(p+20u,32768);put(p+32u,65536);
    p=table(f,8);put(p+20u,1u<<30u);put(p+24u,1u<<30u);put(p+36u,65536);put(p+40u,65536);put(p+60u,1);
    p=table(f,10);put(p+8u,2);
    p=table(f,11);put(p+12u,65536);put(p+24u,1);
    p+=32u;put(p,65536);put(p+12u,65536);put(p+20u,1);
    put(table(f,12),1);
    put(table(f,13)+24u,1u<<30u);
    p=table(f,14);put(p,16);put(p+4u,2);put(p+8u,1);
    put(f->catalog,TAG("C3C1"));put(f->catalog+4u,1);put(f->catalog+8u,1);put(f->catalog+12u,24);
    put(f->catalog+16u,COURSE_ID);put(f->catalog+20u,17);put(f->catalog+28u,2);
    memcpy(f->texture,"MKSCIMG1",8);f->texture[8]=32;f->texture[10]=1;f->texture[12]=f->texture[14]=1;
    put(f->texture+24u,32);put(f->texture+28u,36);put(f->texture+32u,0xff102030);
    f->sections[0]=(PortableAssetSectionView){.kind=99,.content_id=COURSE_ID,.version=1,.bytes=b,.size=f->size};
    f->sections[1]=(PortableAssetSectionView){.kind=100,.content_id=COURSE_ID,.version=1,.bytes=f->catalog,.size=40};
    f->sections[2]=(PortableAssetSectionView){.kind=101,.content_id=333,.version=1,.bytes=f->texture,.size=36};
    f->pack=(PortableAssetPackView){.section_count=3,.resolved_sections=f->sections};
}
static int rejects(Fixture *f,const char *named)
{
    PortableCourse3DView v;memset(&v,0x7f,sizeof v);char error[256];
    CHECK(!portable_course_3d_open(&f->pack,COURSE_ID,&v,error,sizeof error));
    CHECK(v.bytes==NULL && v.table_count==0u && strstr(error,named)!=NULL);
    return 0;
}
static int borrowed_geometry(void)
{
    Fixture f;init(&f);uint8_t before[sizeof f.storage];memcpy(before,f.storage,sizeof before);
    PortableCourse3DView v;char error[256];CHECK(portable_course_3d_open(&f.pack,COURSE_ID,&v,error,sizeof error));
    CHECK(portable_course_3d_validate_pack(&f.pack,error,sizeof error));
    CHECK(v.id==COURSE_ID && v.bytes==data(&f));
    PortableCourse3DMeta meta;CHECK(portable_course_3d_meta(&v,&meta));
    CHECK(!strcmp(meta.name,"Test") && meta.sky_rgba==0x123456ffu && meta.physics_profile_id==0u);
    PortableCourse3DVertex vertex;CHECK(portable_course_3d_vertex(&v,1,&vertex));CHECK(vertex.position_q16[0]==65536 && vertex.normal_q30[2]==(1<<30));
    PortableCourse3DTriangle tri;CHECK(portable_course_3d_triangle(&v,0,&tri));CHECK(tri.vertices[2]==2u && tri.material==0u);
    PortableCourse3DMaterial material;CHECK(portable_course_3d_material(&v,0,&material));CHECK(material.base_rgba==0xabcdef80u);
    PortableCourse3DGrid grid;CHECK(portable_course_3d_grid(&v,0,&grid));CHECK(grid.quaternion_q30[3]==(1<<30));
    CHECK(!portable_course_3d_vertex(&v,3,&vertex));CHECK(portable_course_3d_catalog_count(&f.pack)==1u);
    PortableCourse3DCatalog row;CHECK(portable_course_3d_catalog_at(&f.pack,0,&row,error,sizeof error));CHECK(row.course_id==COURSE_ID && row.cup_id==17u);
    CHECK(!portable_course_3d_catalog_at(&f.pack,1,&row,error,sizeof error));
    CHECK(!memcmp(before,f.storage,sizeof before));
    put(table(&f,4),333);CHECK(portable_course_3d_open(&f.pack,COURSE_ID,&v,error,sizeof error));
    PortableNativeImage image;CHECK(portable_course_3d_texture(&v,333,&image));CHECK(portable_native_image_pixel(&image,0,0)==0xff102030u);
    return 0;
}
static int jump_distance_property(void)
{
    Fixture f;
    init(&f);
    const uint8_t key[]="mksc:jump-max-distance\0";
    _Static_assert(sizeof key%4u==0u, "jump property key must occupy complete rows");
    const uint32_t key_offset=get(data(&f)+64u+8u);
    for (size_t i=0;i<sizeof key;++i)
        CHECK(append_row(&f,0,key+i));
    put(table(&f,14),key_offset);
    put(table(&f,14)+4u,PORTABLE_COURSE_3D_PROPERTY_FIXED);
    put(table(&f,14)+8u,4*65536);
    put(table(&f,7),5);
    put(table(&f,7)+28u,9*65536);
    put(table(&f,7)+44u,1);
    PortableCourse3DView view;
    char error[192];
    CHECK(portable_course_3d_open(&f.pack,COURSE_ID,&view,error,sizeof error));
    CHECK(portable_course_3d_validate_pack(&f.pack,error,sizeof error));
    PortableCourse3DSurface surface;
    CHECK(portable_course_3d_surface(&view,0,&surface) && surface.jump_max_distance_q16==4*65536);
    unsigned char before[sizeof surface];
    memcpy(before,&surface,sizeof before);
    for (unsigned bad=0;bad<6;++bad)
    {
        put(table(&f,14)+4u,bad==3?PORTABLE_COURSE_3D_PROPERTY_INTEGER:PORTABLE_COURSE_3D_PROPERTY_FIXED);
        put(table(&f,14)+8u,bad==0?0:bad==1?UINT32_MAX:bad==2?1024*65536+1:4*65536);
        put(table(&f,7),bad==4?0:5);
        put(table(&f,7)+32u,bad==5?2*65536:65536);
        CHECK(!portable_course_3d_surface(&view,0,&surface));
        CHECK(!memcmp(before,&surface,sizeof before));
        PortableCourse3DView invalid;
        CHECK(!portable_course_3d_open(&f.pack,COURSE_ID,&invalid,error,sizeof error));
        CHECK(strstr(error,"jump-max-distance") && !invalid.table_count);
    }
    put(table(&f,14)+4u,PORTABLE_COURSE_3D_PROPERTY_FIXED);
    put(table(&f,14)+8u,1024*65536);
    put(table(&f,7),5);
    put(table(&f,7)+32u,65536);
    CHECK(portable_course_3d_surface(&view,0,&surface) && surface.jump_max_distance_q16==1024*65536);
    put(table(&f,7)+44u,0);
    CHECK(portable_course_3d_surface(&view,0,&surface) && surface.jump_max_distance_q16==0);
    return 0;
}
static int malformed_content(void)
{
    Fixture f;init(&f);f.sections[0].size=63;CHECK(!rejects(&f,"header"));
    init(&f);put(data(&f)+28u,1);CHECK(!rejects(&f,"header"));
    init(&f);put(data(&f)+64u+20u+4u,f.offsets[0]);CHECK(!rejects(&f,"directory"));
    init(&f);put(data(&f)+64u+2u*20u+8u,UINT32_MAX);CHECK(!rejects(&f,"directory"));
    init(&f);put(data(&f)+64u+14u*20u,TAG("NOPE"));put(data(&f)+64u+14u*20u+16u,1);CHECK(!rejects(&f,"directory"));
    init(&f);table(&f,0)[1]=0xc0;CHECK(!rejects(&f,"STRS"));
    init(&f);put(table(&f,1),25);CHECK(!rejects(&f,"META"));
    init(&f);put(table(&f,2),268435457);CHECK(!rejects(&f,"VERT"));
    init(&f);put(table(&f,2)+20u,0);CHECK(!rejects(&f,"VERT"));
    init(&f);put(table(&f,3)+8u,1);CHECK(!rejects(&f,"TRIS"));
    init(&f);put(table(&f,4),334);CHECK(!rejects(&f,"MATS"));
    init(&f);put(table(&f,4)+36u,48);CHECK(!rejects(&f,"MATS"));
    init(&f);put(table(&f,6)+20u,0);CHECK(!rejects(&f,"COLT"));
    init(&f);put(table(&f,7)+20u,65537);CHECK(!rejects(&f,"SURF"));
    init(&f);put(table(&f,8)+56u,1);CHECK(!rejects(&f,"CPNT"));
    /* Each axis remains a valid unit vector; parallel axes cannot define
     * the checkpoint rectangle promised by the course contract. */
    init(&f);memcpy(table(&f,8)+24u,table(&f,8)+12u,12u);CHECK(!rejects(&f,"CPNT"));
    init(&f);CHECK(append_row(&f,12,NULL));put(table(&f,11)+20u,1);put(table(&f,11)+24u,0);
    put(table(&f,11)+32u+20u,0);put(table(&f,11)+32u+24u,2);CHECK(!rejects(&f,"RTHD"));
    init(&f);CHECK(append_row(&f,12,NULL));put(table(&f,11)+32u+24u,1);put(table(&f,12)+4u,1);CHECK(!rejects(&f,"RTED"));
    init(&f);put(table(&f,13)+24u,0);CHECK(!rejects(&f,"GRID"));
    init(&f);put(table(&f,14)+24u,1);CHECK(!rejects(&f,"PROP"));
    init(&f);put(f.catalog+24u,1);CHECK(!rejects(&f,"C3C1"));
    init(&f);f.pack.section_count=1;CHECK(!rejects(&f,"C3C1"));
    init(&f);f.pack.section_count=0;char error[256];CHECK(portable_course_3d_validate_pack(&f.pack,error,sizeof error));
    CHECK(!portable_course_3d_validate_pack(NULL,error,sizeof error));
    return 0;
}
static int composition_and_cycles(void)
{
    Fixture f,g;init(&f);init(&g);char error[256];PortableCourse3DView view;
    /* A closed route permits its reachable return edge; an open route does not. */
    CHECK(append_row(&f,12,NULL));put(table(&f,11)+32u+24u,1);
    CHECK(!rejects(&f,"RTED"));
    put(table(&f,10)+16u,1);
    CHECK(portable_course_3d_open(&f.pack,COURSE_ID,&view,error,sizeof error));
    /* Catalog rows are independent sections, but two courses cannot occupy
     * one 3D cup slot. Full IDs remain independent of the old byte domain. */
    put(data(&g)+12u,COURSE_ID+1u);g.sections[0].content_id=COURSE_ID+1u;
    put(g.catalog+16u,COURSE_ID+1u);g.sections[1].content_id=COURSE_ID+1u;
    PortableAssetSectionView sections[5]={f.sections[0],f.sections[1],f.sections[2],g.sections[0],g.sections[1]};
    PortableAssetPackView pack={.section_count=5,.resolved_sections=sections};
    CHECK(!portable_course_3d_validate_pack(&pack,error,sizeof error));CHECK(strstr(error,"ambiguous")!=NULL);
    put(g.catalog+24u,1);put(table(&g,1)+24u,1);
    CHECK(portable_course_3d_validate_pack(&pack,error,sizeof error));
    /* Shared assets resolve from the final composed pack, not the course blob. */
    put(table(&f,4),333);f.texture[10]=2;
    CHECK(!portable_course_3d_open(&pack,COURSE_ID,&view,error,sizeof error));CHECK(strstr(error,"texture")!=NULL);
    f.texture[10]=1;
    CHECK(portable_course_3d_open(&pack,COURSE_ID,&view,error,sizeof error));
    CHECK(!portable_course_3d_open(NULL,COURSE_ID,&view,error,sizeof error));CHECK(view.bytes==NULL);
    return 0;
}
static int graph_ownership(void)
{
    Fixture f;
    init(&f);CHECK(append_row(&f,10,table(&f,10)));CHECK(!rejects(&f,"overlapping"));
    init(&f);put(table(&f,10)+8u,1);CHECK(!rejects(&f,"unowned"));
    init(&f);put(table(&f,11)+32u+20u,0);put(table(&f,11)+32u+24u,1);CHECK(!rejects(&f,"overlapping"));
    init(&f);CHECK(append_row(&f,12,NULL));CHECK(!rejects(&f,"unowned"));
    init(&f);CHECK(append_row(&f,9,NULL));CHECK(!rejects(&f,"unowned"));
    init(&f);CHECK(append_row(&f,9,NULL));put(table(&f,8)+56u,1);
    CHECK(append_row(&f,8,table(&f,8)));CHECK(!rejects(&f,"overlapping"));
    return 0;
}
static int invalid_physics_profile(void)
{
    Fixture f;init(&f);char error[256];PortableCourse3DView view;
    /* Missing referenced profiles fail before the borrowed view is published. */
    put(table(&f,1)+88u,42u);
    CHECK(!portable_course_3d_open(&f.pack,COURSE_ID,&view,error,sizeof error));
    CHECK(strstr(error,"physics profile")!=NULL && view.bytes==NULL);
    /* An unused malformed kind102 cannot hide in an otherwise valid mod pack. */
    init(&f);
    const uint8_t bad_profile[32]={0};
    PortableAssetSectionView sections[4]={f.sections[0],f.sections[1],f.sections[2],
        {.kind=102u,.content_id=42u,.version=1u,.bytes=bad_profile,.size=sizeof bad_profile}};
    PortableAssetPackView pack={.section_count=4u,.resolved_sections=sections};
    CHECK(!portable_course_3d_validate_pack(&pack,error,sizeof error));
    CHECK(strstr(error,"KPH2")!=NULL);
    return 0;
}
static int typed_properties(void)
{
    Fixture f;
    init(&f);
    const uint32_t types[6] = {1, 3, 4, 6, 7, 0};
    for (unsigned i = 0; i < 6u; ++i)
    {
        uint8_t row[32] = {0};
        put(row, 16);
        put(row + 4u, types[i]);
        if (i == 0u)
            put(row + 8u, (uint32_t)-32768);
        if (i == 1u)
            put(row + 8u, 5);
        if (i == 2u)
        {
            put(row + 8u, 101);
            put(row + 12u, 333);
        }
        if (i == 4u)
        {
            put(row + 8u, (uint32_t)-65536);
            put(row + 12u, 32768);
            put(row + 16u, INT32_MAX);
        }
        if (i == 5u)
            put(row + 8u, UINT32_C(0x80000000));
        CHECK(append_row(&f, 14, row));
    }
    PortableCourse3DView view;
    char error[256];
    CHECK(portable_course_3d_open(&f.pack, COURSE_ID, &view, error, sizeof error));
    const Fixture before = f;
    PortableCourse3DProperty property;
    CHECK(portable_course_3d_property_at(&view, 0, &property));
    CHECK(property.type == PORTABLE_COURSE_3D_PROPERTY_BOOLEAN && property.value.boolean == 1u);
    CHECK(!strcmp(property.key, "demo:key"));
    CHECK(portable_course_3d_property_at(&view, 1, &property));
    CHECK(property.type == PORTABLE_COURSE_3D_PROPERTY_FIXED && property.value.fixed_q16 == -32768);
    CHECK(portable_course_3d_property_at(&view, 2, &property));
    CHECK(property.type == PORTABLE_COURSE_3D_PROPERTY_STRING &&
          !strcmp(property.value.string, "Author"));
    CHECK(property.value.string == (const char *)table(&f, 0) + 5u);
    CHECK(portable_course_3d_property_at(&view, 3, &property));
    CHECK(property.type == PORTABLE_COURSE_3D_PROPERTY_ASSET && property.value.asset.kind == 101u &&
          property.value.asset.id == 333u);
    CHECK(portable_course_3d_property_at(&view, 4, &property));
    CHECK(property.type == PORTABLE_COURSE_3D_PROPERTY_ROUTE && property.value.route == 0u);
    CHECK(portable_course_3d_property_at(&view, 5, &property));
    CHECK(property.type == PORTABLE_COURSE_3D_PROPERTY_VECTOR &&
          property.value.vector_q16[0] == -65536 && property.value.vector_q16[2] == INT32_MAX);
    CHECK(portable_course_3d_property_find(&view, 0, 7, "demo:key", &property));
    CHECK(property.type == PORTABLE_COURSE_3D_PROPERTY_INTEGER &&
          property.value.integer == INT32_MIN);
    CHECK(portable_course_3d_property_find(&view, 0, 2, "demo:key", &property));
    CHECK(property.type == PORTABLE_COURSE_3D_PROPERTY_FIXED);
    const PortableCourse3DProperty saved = property;
    CHECK(!portable_course_3d_property_find(&view, 0, 7, "other:key", &property));
    CHECK(!portable_course_3d_property_find(&view, 7, 1, "demo:key", &property));
    CHECK(!portable_course_3d_property_find(&view, UINT32_MAX, 2, "demo:key", &property));
    CHECK(!portable_course_3d_property_find(&view, 7, 0, "demo:key", &property));
    CHECK(!portable_course_3d_property_at(&view, 7, &property));
    CHECK(!portable_course_3d_property_at(NULL, 0, &property));
    CHECK(!memcmp(&saved, &property, sizeof saved));
    CHECK(!memcmp(&before, &f, sizeof f));
    /* A later malformed row must not publish an earlier matching candidate. */
    put(table(&f, 14) + 6u * 32u + 24u, 1);
    CHECK(!portable_course_3d_property_find(&view, 0, 7, "demo:key", &property));
    CHECK(!memcmp(&saved, &property, sizeof saved));
    put(table(&f, 14) + 6u * 32u + 24u, 0);
    /* Invalid values never overwrite a previous valid result. */
    put(table(&f, 14) + 8u, 2);
    CHECK(!portable_course_3d_property_at(&view, 0, &property));
    CHECK(!portable_course_3d_property_find(&view, 0, 7, "demo:key", &property));
    put(table(&f, 14) + 8u, 1);
    put(table(&f, 14) + 2u * 32u + 8u, UINT32_MAX);
    CHECK(!portable_course_3d_property_at(&view, 2, &property));
    put(table(&f, 14) + 3u * 32u + 12u, 334);
    CHECK(!portable_course_3d_property_at(&view, 3, &property));
    put(table(&f, 14) + 4u * 32u + 8u, 1);
    CHECK(!portable_course_3d_property_at(&view, 4, &property));
    CHECK(!memcmp(&saved, &property, sizeof saved));
    return 0;
}
static int class_profiles(void)
{
    Fixture f;
    init(&f);
    /* Authored KPH2 fixtures; all class variants share valid handling but have
     * independently selected ground speeds. */
    uint8_t profiles[4][PORTABLE_MESH_KART_RULES_SIZE];
    const uint32_t ids[4] = {1, 50, 100, 150}, caps[4] = {24, 12, 18, 24};
    for (unsigned i = 0; i < 4u; ++i)
    {
        PortableMeshKartProfile profile = test_mesh_profile();
        profile.profile_id = ids[i];
        profile.ranges[PORTABLE_MESH_KART_RANGE_SPEED_GROUND].level0_q16 =
            profile.ranges[PORTABLE_MESH_KART_RANGE_SPEED_GROUND].level19_q16 = (int32_t)caps[i] * 65536;
        test_mesh_profile_payload(&profile, profiles[i]);
        f.sections[3u + i] = (PortableAssetSectionView){.kind = 102, .content_id = ids[i],
            .version = PORTABLE_MESH_KART_RULES_VERSION, .bytes = profiles[i], .size = sizeof profiles[i]};
    }
    f.pack.section_count = 7;
    put(table(&f, 1) + 88u, 1);
    const char *keys[3] = {"mksc:physics-50cc", "mksc:physics-100cc", "mksc:physics-150cc"};
    for (unsigned i = 0; i < 3u; ++i)
    {
        const uint32_t offset = get(data(&f) + 64u + 8u);
        for (size_t c = 0; c <= strlen(keys[i]); ++c)
        {
            const uint8_t ch = (uint8_t)keys[i][c];
            CHECK(append_row(&f, 0, &ch));
        }
        uint8_t property[32] = {0};
        put(property, offset);
        put(property + 4u, 4);
        put(property + 8u, 102);
        put(property + 12u, ids[i + 1u]);
        CHECK(append_row(&f, 14, property));
    }
    put(table(&f, 1) + 100u, 4);
    PortableCourse3DView view;
    char error[256];
    CHECK(portable_course_3d_open(&f.pack, COURSE_ID, &view, error, sizeof error));
    CHECK(portable_course_3d_validate_pack(&f.pack, error, sizeof error));
    for (unsigned c = 0; c < 3u; ++c)
    {
        uint32_t selected = 999;
        CHECK(portable_course_3d_physics_profile(&view, c, &selected, error, sizeof error));
        CHECK(selected == ids[c + 1u] && portable_course_3d_has_physics_profile(&view, selected));
    }
    CHECK(portable_course_3d_has_physics_profile(&view, 1));
    CHECK(!portable_course_3d_has_physics_profile(&view, 123));
    uint32_t selected = 999;
    CHECK(!portable_course_3d_physics_profile(&view, 3, &selected, error, sizeof error) &&
          selected == 999);
    /* Missing mappings fall back, not to a compiled class multiplier. */
    put(table(&f, 1) + 100u, 1);
    CHECK(portable_course_3d_open(&f.pack, COURSE_ID, &view, error, sizeof error));
    CHECK(portable_course_3d_physics_profile(&view, 2, &selected, error, sizeof error) &&
          selected == 1);
    put(table(&f, 1) + 100u, 4);
    CHECK(portable_course_3d_open(&f.pack, COURSE_ID, &view, error, sizeof error));
    /* A valid reference of the wrong kind is rejected before publication. */
    put(table(&f, 14) + 32u + 8u, 101);
    put(table(&f, 14) + 32u + 12u, 333);
    CHECK(!portable_course_3d_validate_pack(&f.pack, error, sizeof error));
    selected = 999;
    CHECK(!portable_course_3d_physics_profile(&view, 0, &selected, error, sizeof error) &&
          selected == 999);
    put(table(&f, 14) + 32u + 8u, 102);
    put(table(&f, 14) + 32u + 12u, 50);
    put(profiles[1], 0);
    CHECK(!portable_course_3d_validate_pack(&f.pack, error, sizeof error));
    put(profiles[1], 0x3148504bu);
    put(table(&f, 14) + 32u + 12u, 151);
    CHECK(!portable_course_3d_validate_pack(&f.pack, error, sizeof error));
    put(table(&f, 14) + 32u + 12u, 50);
    put(table(&f, 1) + 88u, 0);
    CHECK(portable_course_3d_open(&f.pack, COURSE_ID, &view, error, sizeof error));
    CHECK(!portable_course_3d_physics_profile(&view, 0, &selected, error, sizeof error) &&
          selected == 999);
    CHECK(!portable_course_3d_has_physics_profile(&view, 50));
    return 0;
}
static int typed_items(void)
{
    Fixture f;
    init(&f);
    PortableCourse3DView view;
    char error[192];
    CHECK(portable_course_3d_open(&f.pack, COURSE_ID, &view, error, sizeof error));
    uint8_t rows[64] = {0};
    const int32_t positions[2][3] = {{-65536, 32768, 4 * 65536}, {-65536, 32768, -3 * 65536}};
    for (unsigned i = 0; i < 2u; ++i)
    {
        for (unsigned axis = 0; axis < 3u; ++axis)
            put(rows + i * 32u + axis * 4u, (uint32_t)positions[i][axis]);
        put(rows + i * 32u + 12u, 32768 + i);
        put(rows + i * 32u + 16u, i ? 65535 : 16384);
        put(rows + i * 32u + 20u, i ? UINT32_MAX : 180);
        put(rows + i * 32u + 28u, 1);
    }
    PortableCourse3DTable *items = &view.tables[view.table_count++];
    *items = (PortableCourse3DTable){TAG("ITEM"), 2, 32, rows};
    const Fixture before = f;
    uint8_t saved_rows[64];
    memcpy(saved_rows, rows, sizeof rows);
    PortableCourse3DItem item;
    CHECK(portable_course_3d_item(&view, 0, &item));
    CHECK(item.position_q16[0] == -65536 && item.position_q16[1] == 32768 &&
          item.position_q16[2] == 4 * 65536 && item.radius_q16 == 32768 &&
          item.yaw_turns == 16384 && item.respawn_ticks == 180 && item.property_first == 0 &&
          item.property_count == 1);
    CHECK(portable_course_3d_item(&view, 1, &item) && item.position_q16[2] == -3 * 65536 &&
          item.yaw_turns == 65535 && item.respawn_ticks == UINT32_MAX);
    CHECK(!memcmp(&f, &before, sizeof f) && !memcmp(rows, saved_rows, sizeof rows));
    const PortableCourse3DItem original = item;
    CHECK(!portable_course_3d_item(&view, 2, &item) && !memcmp(&item, &original, sizeof item));
    CHECK(!portable_course_3d_item(&view, UINT32_MAX, &item) &&
          !memcmp(&item, &original, sizeof item));
    CHECK(!portable_course_3d_item(NULL, 0, &item) && !memcmp(&item, &original, sizeof item));
    CHECK(!portable_course_3d_item(&view, 0, NULL));
    items->stride = 31;
    CHECK(!portable_course_3d_item(&view, 0, &item) && !memcmp(&item, &original, sizeof item));
    items->stride = 32;
    items->bytes = NULL;
    CHECK(!portable_course_3d_item(&view, 0, &item) && !memcmp(&item, &original, sizeof item));
    items->bytes = rows;
    const struct
    {
        unsigned offset;
        uint32_t value;
    } invalid[] = {{0, 4096u * 65536u + 1u},
                   {12, 0},
                   {12, UINT32_MAX},
                   {16, 65536},
                   {20, 0},
                   {24, 1},
                   {28, 2}};
    for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; ++i)
    {
        put(rows + invalid[i].offset, invalid[i].value);
        CHECK(!portable_course_3d_item(&view, 0, &item) && !memcmp(&item, &original, sizeof item));
        memcpy(rows, saved_rows, sizeof rows);
    }
    return 0;
}
/* RESP replaces the fixture's final optional PROP row without changing any
 * original fixture bytes. The extra sixteen bytes stay inside owned storage. */
static void arena_respawn(Fixture *f)
{
    uint8_t *entry=data(f)+64u+14u*20u;
    put(entry,TAG("RESP"));put(entry+12u,48u);
    memset(table(f,14),0,48u);
    put(table(f,14)+24u,1u<<30u);
    put(table(f,14)+28u,UINT32_MAX);
    put(table(f,14)+32u,65536u);
    put(table(f,14)+40u,UINT32_MAX);
    put(table(f,1)+100u,0u);
    f->size+=16u;put(data(f)+8u,(uint32_t)f->size);f->sections[0].size=f->size;
}
static void fixture_mode(Fixture *f,uint32_t mode,uint32_t laps)
{
    put(table(f,1)+12u,mode);put(table(f,1)+16u,laps);put(f->catalog+28u,mode);
}
static int battle_arena(void)
{
    Fixture f;init(&f);arena_respawn(&f);fixture_mode(&f,16u,0u);
    put(data(&f)+64u+8u*20u+8u,0u);
    PortableCourse3DView view;char error[256];
    uint8_t before[sizeof f.storage];memcpy(before,f.storage,sizeof before);
    CHECK(portable_course_3d_open(&f.pack,COURSE_ID,&view,error,sizeof error));
    CHECK(portable_course_3d_validate_pack(&f.pack,error,sizeof error));
    CHECK(!memcmp(before,f.storage,sizeof before));
    PortableCourse3DMeta meta;PortableCourse3DRespawn respawn;
    CHECK(portable_course_3d_meta(&view,&meta) && meta.modes==16u && meta.laps==0u);
    CHECK(portable_course_3d_table(&view,TAG("CPNT"))->count==0u);
    CHECK(portable_course_3d_respawn(&view,0u,&respawn) && respawn.checkpoint==UINT32_MAX);
    put(table(&f,14)+28u,0u);CHECK(rejects(&f,"RESP")==0);
    put(table(&f,14)+28u,UINT32_MAX);fixture_mode(&f,18u,1u);
    CHECK(rejects(&f,"META")==0); /* Lap capability still needs gates. */
    put(data(&f)+64u+8u*20u+8u,1u);fixture_mode(&f,18u,0u);
    CHECK(rejects(&f,"META")==0);
    fixture_mode(&f,18u,1u);CHECK(rejects(&f,"RESP")==0);
    put(table(&f,14)+28u,0u);
    CHECK(portable_course_3d_open(&f.pack,COURSE_ID,&view,error,sizeof error));
    put(table(&f,14)+28u,1u);CHECK(rejects(&f,"RESP")==0);
    fixture_mode(&f,16u,0u);put(table(&f,14)+28u,0u);
    CHECK(portable_course_3d_open(&f.pack,COURSE_ID,&view,error,sizeof error));
    return 0;
}
int main(void)
{
    CHECK(jump_distance_property()==0); CHECK(battle_arena()==0); CHECK(class_profiles()==0); CHECK(typed_items()==0); return typed_properties() || borrowed_geometry() || malformed_content() || composition_and_cycles() || graph_ownership() || invalid_physics_profile(); }
