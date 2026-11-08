#include "cannonball_sound.h"
#include <stdio.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"cannonball line%d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void)
{
    PortableCannonball a;
    CHECK(!portable_cannonball_init(&a,0,0,0u,4u));
    for(unsigned variant=0;variant<4;++variant){
        CHECK(portable_cannonball_init(&a,100*65536,200*65536,2u,(uint16_t)variant));
        for(unsigned i=0;i<variant*32;++i){CHECK(portable_cannonball_advance(&a));CHECK(a.state==1 && a.flags==0);}
        CHECK(portable_cannonball_advance(&a));
        CHECK(a.state==3 && a.timer==999 && a.height_q16==0x27f800 && a.affine_x_q8==1000 && a.flags==3);
        unsigned falling=1;
        while(a.state==3){CHECK(portable_cannonball_advance(&a));CHECK(++falling<60);}
        CHECK(falling==51 && a.state==4 && a.timer==1 && a.height_q16==0 && a.affine_x_q8==256);
        CHECK(portable_cannonball_advance(&a));
        CHECK(a.state==5 && a.timer==31 && a.art==6 && a.size==4 && a.flags==1);
        for(int timer=30;timer>=0;--timer){
            CHECK(portable_cannonball_advance(&a));
            const int frame=timer>=28?1:timer>=24?2:timer>=20?3:timer>=14?4:timer>=8?5:6;
            CHECK(a.state==5 && a.timer==timer && a.art==frame && a.size==20 && a.flags==1);
        }
        CHECK(portable_cannonball_advance(&a));CHECK(a.state==1 && a.timer==64 && a.size==4 && a.flags==0);
        for(unsigned i=0;i<64;++i){CHECK(portable_cannonball_advance(&a));CHECK(a.state==1);}
        CHECK(portable_cannonball_advance(&a));CHECK(a.state==3 && a.art==0 && a.flags==3);
    }
    PortableCannonballListener listener={0};
    PortableCannonballSound sound;
    CHECK(portable_cannonball_init(&a,0,0,0,0));
    CHECK(portable_cannonball_sound_request(&a,&listener,1,&sound));
    CHECK(sound.song_id==382u && sound.volume==256u && sound.require_idle);
    CHECK(!portable_cannonball_sound_request(&a,&listener,0,&sound));
    a.x_q16=443*65536;
    CHECK(portable_cannonball_sound_request(&a,&listener,1,&sound) && sound.volume==65u);
    a.x_q16=444*65536;
    CHECK(portable_cannonball_sound_request(&a,&listener,1,&sound) && sound.volume==64u);
    a.x_q16=445*65536;
    CHECK(!portable_cannonball_sound_request(&a,&listener,1,&sound));
    a.x_q16=0;a.state=4;a.timer=1;
    CHECK(portable_cannonball_sound_request(&a,&listener,0,&sound));
    CHECK(sound.song_id==383u && sound.volume==256u && !sound.require_idle);
    listener.driver_flags=0x20u;
    CHECK(!portable_cannonball_sound_request(&a,&listener,1,&sound));
    listener.driver_flags=0;a.state=5;a.timer=30;
    CHECK(!portable_cannonball_sound_request(&a,&listener,1,&sound));
    CHECK(portable_spatial_audio_volume(0,0,0x0000ffffu,0)==256u);
    CHECK(portable_spatial_audio_volume(0,0,0x00010000u,0)==256u);
    CHECK(portable_spatial_audio_volume(512u*65536u,0,0,0)==0u);
    a.state=6;CHECK(!portable_cannonball_advance(&a));
    return 0;
}
