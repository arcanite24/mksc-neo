#include "seagull.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"seagull line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void)
{
    int32_t sine[1024];for(int i=0;i<1024;++i)sine[i]=i*100;
    PortableSeagull a;
    CHECK(!portable_seagull_init(&a,0,0,0u,1u));
    CHECK(portable_seagull_init(&a,100*65536,200*65536,2u,0u));
    CHECK(a.affine_x_q8==224u && a.affine_y_q8==224u);
    CHECK(portable_seagull_advance(&a,sine));
    CHECK(a.state==1 && a.affine_x_q8==256u && a.height_q16==0);
    PortableSeagull before=a;
    portable_seagull_contact(&a,0x2001u,1,0);CHECK(!memcmp(&a,&before,sizeof a));
    portable_seagull_contact(&a,0x1001u,0,1);CHECK(!memcmp(&a,&before,sizeof a));
    portable_seagull_contact(&a,0x2001u,0,0);CHECK(a.state==16);
    for(int tick=1;tick<=40;++tick){
        CHECK(portable_seagull_advance(&a,sine));
        CHECK(a.timer==tick%5 && a.phase==tick/5);
        CHECK(a.height_q16==sine[((tick/5)*4+tick%5)*16]*20);
        if(tick<40)CHECK(a.art==1u+(unsigned)((tick/5)&1) && a.state==17);
        else CHECK(a.state==0);
        portable_seagull_contact(&a,0x2001u,0,0);
        if(tick<40)CHECK(a.state==17); /* repeated overlap never restarts flight */
    }
    CHECK(a.state==16); /* contact on the completed flight can start another */
    portable_seagull_contact(&a,0x2001u,0,1);CHECK(a.state==240);
    a.height_q16=0;
    for(int tick=1;tick<=26;++tick){
        CHECK(portable_seagull_advance(&a,sine));
        CHECK(a.height_q16==tick*0x30000-(tick*(tick-1)/2)*0x4000);
        CHECK(a.flip_y && a.affine_y_q8==256u && !(a.flags&2u));
        CHECK(a.state==(tick==26?243:241));
    }
    CHECK(!(a.flags&1u));before=a;
    portable_seagull_contact(&a,0x2001u,0,1);CHECK(!memcmp(&a,&before,sizeof a));
    CHECK(portable_seagull_advance(&a,sine));CHECK(!memcmp(&a,&before,sizeof a));
    a.state=18;CHECK(!portable_seagull_advance(&a,sine));
    return 0;
}
