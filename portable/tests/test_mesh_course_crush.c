#include "mesh_course_crush.h"
#include <stdio.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"crush line %d: %s\n",__LINE__,#c); return 1; } } while (0)
static PortableMeshCrushRules rules(void)
{
    PortableMeshCrushRules r={0};
    for (unsigned i=0;i<5;++i) { r.counts[i]=1;r.rows[i][0]=(PortableDriverAffineRow){256,256,1}; }
    r.rows[2][0]=(PortableDriverAffineRow){192,512,110};
    r.rows[3][0]=(PortableDriverAffineRow){192,512,18};
    r.counts[4]=2;r.rows[4][0]=(PortableDriverAffineRow){192,512,112};
    r.rows[4][1]=(PortableDriverAffineRow){256,256,1};
    return r;
}
int main(void)
{
    PortableMeshCrushRules r=rules();
    PortableMeshCrushState s={0};
    PortableMeshKartState k={0};
    k.up_q30[2]=k.forward_q30[0]=1073741824;
    k.velocity_q16[0]=k.speed_q16=4*65536;
    k.previous_buttons=PORTABLE_MESH_HOP;
    k.drift_charge=30;k.boost_ticks[0]=10;
    CHECK(portable_mesh_course_crush_admit(&r,&s,&k,NULL,0));
    CHECK(s.contact_timer==0x7000 && s.affine.mode==5 && s.affine.remaining==109);
    CHECK(!k.speed_q16 && !k.velocity_q16[0] && !k.drift_charge && !k.boost_ticks[0]);
    CHECK(k.previous_buttons==PORTABLE_MESH_HOP);
    for (unsigned tick=1;tick<=241;++tick) {
        int held=-1,cue=-1;
        CHECK(portable_mesh_course_crush_tick(&r,&s,0,&held,&cue,NULL,0));
        CHECK(held==(tick<=128));
        CHECK(cue==(tick==239));
        CHECK(s.contact_timer==(tick<128?0x7000-tick:0));
        if (tick==109) CHECK(s.affine.mode==6 && s.affine.remaining==18);
        if (tick==127) CHECK(!s.affine.mode && s.affine.inverse_y_q8==512);
        if (tick==128) CHECK(s.affine.mode==7 && s.affine.remaining==111);
    }
    CHECK(!s.affine.mode && s.affine.inverse_x_q8==256 && s.affine.inverse_y_q8==256);
    CHECK(portable_mesh_course_crush_admit(&r,&s,&k,NULL,0));
    int held=-1,cue=-1;
    CHECK(portable_mesh_course_crush_tick(&r,&s,1,&held,&cue,NULL,0));
    CHECK(!s.contact_timer && !s.affine.mode && !s.affine.inverse_x_q8 && !held && !cue);
    CHECK(portable_mesh_course_crush_tick(NULL,&s,0,&held,&cue,NULL,0));
    CHECK(!held && !cue);
    CHECK(portable_mesh_course_crush_admit(&r,&s,&k,NULL,0));
    s.affine.remaining=127;
    const PortableMeshCrushState before=s;
    held=31;cue=47;
    CHECK(!portable_mesh_course_crush_tick(&r,&s,0,&held,&cue,NULL,0));
    CHECK(!memcmp(&before,&s,sizeof s) && held==31 && cue==47);
    r=rules();r.rows[4][0].duration=0;
    const PortableMeshKartState original=k;
    CHECK(!portable_mesh_course_crush_admit(&r,&s,&k,NULL,0));
    CHECK(!memcmp(&original,&k,sizeof k));
    k.velocity_q16[0]=4*65536;k.velocity_q16[1]=-4*65536;
    CHECK(portable_mesh_course_thwomp_rebound(&k,32768,NULL,0));
    CHECK(k.velocity_q16[0]<0 && k.velocity_q16[1]>0 && k.velocity_q16[2]==0);
    const PortableMeshKartState reflected=k;
    CHECK(!portable_mesh_course_thwomp_rebound(&k,0,NULL,0));
    CHECK(!memcmp(&reflected,&k,sizeof k));
    puts("3D Thwomp crush: PASS hold release source affine recovery rescue rebound rollback");
    return 0;
}
