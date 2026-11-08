#include "mesh_course_crush.h"
#include "asset_pack.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
static const uint8_t modes[5] = {1,4,5,6,7};
static int fail(char *error, size_t cap, const char *why)
{
    if (error && cap) snprintf(error, cap, "3D Thwomp crush: %s", why);
    return 0;
}
static uint32_t word(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static uint16_t half(const uint8_t *p) { return (uint16_t)((unsigned)p[0] | (unsigned)p[1]<<8); }
static PortableDriverAffineRules borrowed(const PortableMeshCrushRules *r)
{
    PortableDriverAffineRules out;
    for (unsigned i=0;i<5;++i)
        out.tables[i]=(PortableDriverAffineTable){modes[i],r->counts[i],r->rows[i]};
    return out;
}
int portable_mesh_course_crush_rules_valid(const PortableMeshCrushRules *r)
{
    if (!r) return 0;
    for (unsigned i=0;i<5;++i) {
        if (!r->counts[i] || r->counts[i]>32) return 0;
        for (unsigned j=0;j<r->counts[i];++j)
            if (!r->rows[i][j].inverse_x_q8 || !r->rows[i][j].inverse_y_q8 ||
                !r->rows[i][j].duration || r->rows[i][j].duration>127) return 0;
    }
    return 1;
}
int portable_mesh_course_crush_rules_open(const PortableCourse3DView *course,
    PortableMeshCrushRules *out, char *error, size_t cap)
{
    PortableCourse3DMeta meta;
    PortableCourse3DProperty p;
    PortableAssetSectionView section;
    if (!course || !out || !portable_course_3d_meta(course,&meta) ||
        !portable_course_3d_property_find(course,meta.property_first,meta.property_count,
            "mksc:actor-affine-rules",&p) || p.type!=PORTABLE_COURSE_3D_PROPERTY_ASSET ||
        p.value.asset.kind!=PORTABLE_ASSET_SECTION_DRIVER_AFFINE_RULES ||
        portable_asset_pack_find_section(course->pack,p.value.asset.kind,p.value.asset.id,&section)!=PORTABLE_ASSET_PACK_OK ||
        section.version!=1 || section.size<8 || !section.bytes ||
        word(section.bytes)!=UINT32_C(0x31524641) || word(section.bytes+4)!=5)
        return fail(error,cap,"missing or invalid actor-affine AFR1 rules");
    PortableMeshCrushRules next={0};
    size_t at=8;
    for (unsigned i=0;i<5;++i) {
        if (section.size-at<8) return fail(error,cap,"truncated affine table");
        const uint32_t mode=word(section.bytes+at), count=word(section.bytes+at+4);
        at+=8;
        if (mode!=modes[i] || !count || count>32 || count*6>section.size-at)
            return fail(error,cap,"invalid affine table");
        next.counts[i]=(uint8_t)count;
        for (unsigned j=0;j<count;++j,at+=6) {
            const uint8_t *row=section.bytes+at;
            if (row[5]) return fail(error,cap,"nonzero affine reserve");
            next.rows[i][j]=(PortableDriverAffineRow){half(row),half(row+2),row[4]};
        }
    }
    if (at!=section.size || !portable_mesh_course_crush_rules_valid(&next))
        return fail(error,cap,"invalid affine rows");
    *out=next;
    return 1;
}
int portable_mesh_course_crush_valid(const PortableMeshCrushRules *r, const PortableMeshCrushState *s)
{
    if (!s) return 0;
    if (!s->contact_timer && !s->affine.inverse_x_q8 && !s->affine.inverse_y_q8 &&
        !s->affine.mode && !s->affine.row && !s->affine.remaining && !s->affine.pending_mode) return 1;
    if (!portable_mesh_course_crush_rules_valid(r) || s->contact_timer>0x7000 ||
        (s->contact_timer && s->contact_timer<=0x6f80) || s->affine.pending_mode ||
        !s->affine.inverse_x_q8 || !s->affine.inverse_y_q8) return 0;
    if (!s->affine.mode) return !s->affine.row && !s->affine.remaining;
    for (unsigned i=0;i<5;++i) if (s->affine.mode==modes[i])
        return s->affine.row<r->counts[i] && s->affine.remaining &&
               s->affine.remaining<=r->rows[i][s->affine.row].duration;
    return 0;
}
static void stop_horizontal(PortableMeshKartState *k)
{
    int64_t vertical=0;
    for (unsigned i=0;i<3;++i) vertical+=(int64_t)k->velocity_q16[i]*k->up_q30[i];
    vertical/=1073741824;
    for (unsigned i=0;i<3;++i) k->velocity_q16[i]=(int32_t)(vertical*k->up_q30[i]/1073741824);
    k->speed_q16=0;
    k->steer_filtered_q16=k->outside_drift_q16=0;
    k->drift_charge=k->panel_ticks=k->rocket_ticks=0;
    k->drift_direction=0;k->drift_tier=0;k->last_boost_tier=0;
    memset(k->boost_ticks,0,sizeof k->boost_ticks);
}
void portable_mesh_course_crush_hold(PortableMeshKartState *k, PortableMeshKartEffects *effects)
{
    stop_horizontal(k);
    effects->inhibit_controls=effects->cancel_drift=1;
    effects->boost_active=effects->star=0;effects->boost_acceleration_q16=0;
}
int portable_mesh_course_crush_admit(const PortableMeshCrushRules *r, PortableMeshCrushState *s,
    PortableMeshKartState *k, char *error, size_t cap)
{
    if (!portable_mesh_course_crush_rules_valid(r) || !s || !k || !portable_mesh_kart_state_valid(k))
        return fail(error,cap,"invalid crush admission");
    PortableMeshCrushState next={.contact_timer=0x7000,.affine={.inverse_x_q8=256,.inverse_y_q8=256}};
    const PortableDriverAffineRules rules=borrowed(r);
    if (portable_driver_affine_request(&next.affine,&rules,5)<0 ||
        portable_driver_affine_tick(&next.affine,&rules)<0) return fail(error,cap,"invalid crush affine");
    stop_horizontal(k);
    *s=next;
    return 1;
}
int portable_mesh_course_crush_tick(const PortableMeshCrushRules *r, PortableMeshCrushState *s,
    int recovering, int *held, int *cue, char *error, size_t cap)
{
    if (!held || !cue || !portable_mesh_course_crush_valid(r,s) || recovering<0 || recovering>1)
        return fail(error,cap,"invalid crush turn");
    PortableMeshCrushState next=*s;
    int hold=0, sound=0;
    if (recovering) memset(&next,0,sizeof next);
    else {
        hold=next.contact_timer!=0;
        if (!r) { *held=0; *cue=0; return 1; }
        const PortableDriverAffineRules rules=borrowed(r);
        if (next.contact_timer && --next.contact_timer==0x6f80) {
            next.contact_timer=0;
            if (portable_driver_affine_request(&next.affine,&rules,7)<0)
                return fail(error,cap,"invalid recovery affine");
        }
        if (next.affine.mode) {
            sound=portable_driver_affine_tick(&next.affine,&rules);
            if (sound<0) return fail(error,cap,"invalid affine turn");
        }
    }
    *s=next;*held=hold;*cue=sound;
    return 1;
}
static int32_t rebound(int32_t component)
{
    int32_t reflected=-component;
    if (reflected>=0) reflected=reflected<=128?128:reflected>1023?1024:reflected;
    else if (component<=128) reflected=-128;
    else if (component>1023) reflected=-1024;
    return reflected/2;
}
int portable_mesh_course_thwomp_rebound(PortableMeshKartState *k, int32_t scale,
    char *error, size_t cap)
{
    if (!k || !portable_mesh_kart_state_valid(k) || scale<1311 || scale>131072)
        return fail(error,cap,"invalid rebound");
    PortableMeshKartState next=*k;
    for (unsigned i=0;i<2;++i) {
        const int64_t value=(int64_t)k->velocity_q16[i]*4389*256/((int64_t)262144*scale);
        const uint16_t bits=(uint16_t)((uint64_t)value&65535u);
        const int32_t component=bits<=32767?bits:(int32_t)bits-65536;
        next.velocity_q16[i]=(int32_t)((int64_t)rebound(component)*scale*1024/4389);
    }
    if (portable_mesh_kart_project_travel_speed(&next, &next.speed_q16) != PORTABLE_MESH_OK)
        return fail(error,cap,"rebound speed outside SI bounds");
    if (!portable_mesh_kart_state_valid(&next)) return fail(error,cap,"rebound outside SI bounds");
    *k=next;return 1;
}
