#include "test_support.h"
#include "snow_weather.h"
#define SNOW_CHECK(x) do { if(!(x)){fprintf(stderr,"snow weather line %d: %s\n",__LINE__,#x);return 1;} } while(0)
typedef struct { uint32_t values[64],calls,state;uint8_t lcg; } SnowRandom;
static uint32_t snow_random(void *context)
{
    SnowRandom *r=context;
    if(r->lcg){r->state=r->state*UINT32_C(1103515245)+12345u;++r->calls;
        return (r->state*2u)>>17u;}
    return r->values[(r->calls++)&63u];
}
static void snow_wait(PortableSnowWeatherState *state)
{ for(unsigned i=0;i<12u;++i)state->particles[i].phase=1000; }
int run_snow_weather(void)
{
    /* Invented choices/sine samples isolate recovered arithmetic, no ROM tables. */
    PortableSnowWeatherRules rules={.enabled=1,.count=12,
        .backward={3,2,1,0,3,2,1,0},.forward={0,1,2,3,0,1,2,3}};
    int32_t sine[1024]={0};
    PortableSnowWeatherInputs in={.yaw=16384};
    PortableSnowWeatherState state;
    SnowRandom rng={.values={2,123,7,4}};
    SNOW_CHECK(portable_snow_weather_init(&state,&rules,0,0,16384,1)==1);
    for(unsigned i=0;i<12u;++i)SNOW_CHECK(state.particles[i].y_q16==-16*65536 &&
        state.particles[i].phase==i && !state.particles[i].active);
    SNOW_CHECK(!rng.calls && portable_snow_weather_tick(&state,&rules,&in,sine,1024,snow_random,&rng)==1);
    SNOW_CHECK(rng.calls==4 && state.particles[0].stage==2 &&
        state.particles[0].phase==123 && state.particles[0].x_q16==35*65536 &&
        state.particles[0].y_q16==-4*65536 && !state.particles[0].travel_q16 &&
        state.snapshot.particles[0].visible && state.snapshot.particles[0].art==1 &&
        state.snapshot.particles[0].y==252 && !state.snapshot.particles[1].visible);
    for(unsigned i=1;i<12u;++i)SNOW_CHECK(state.particles[i].phase==i-1u);
    PortableSnowWeatherState saved=state;
    const uint64_t saved_digest=portable_snow_weather_digest(&state);
    in.paused=1;in.parity=1;in.camera_x_q16=12345;
    SNOW_CHECK(portable_snow_weather_tick(&state,&rules,&in,NULL,0,NULL,NULL)==1 &&
        memcmp(&state,&saved,sizeof state)==0 && rng.calls==4 &&
        portable_snow_weather_digest(&state)==saved_digest);
    in=(PortableSnowWeatherInputs){.yaw=16384};
    SNOW_CHECK(portable_snow_weather_tick(&state,&rules,&in,sine,1023,snow_random,&rng)==-1 &&
        memcmp(&state,&saved,sizeof state)==0);
    PortableSnowWeatherRules bad=rules;bad.count=13;
    SNOW_CHECK(!portable_snow_weather_rules_valid(&bad));bad=rules;bad.forward[2]=4;
    SNOW_CHECK(!portable_snow_weather_rules_valid(&bad));
    SNOW_CHECK(portable_snow_weather_init(&state,NULL,0,0,0,1)==0 && !state.enabled);
    SNOW_CHECK(portable_snow_weather_init(&state,&rules,0,0,16384,0)==0 && !state.enabled);
    SNOW_CHECK(portable_snow_weather_tick(&state,&rules,&in,sine,1024,snow_random,&rng)==0);

    /* All twelve advance once; only parity-selected six enter the snapshot. */
    portable_snow_weather_init(&state,&rules,0,0,16384,1);rng=(SnowRandom){0};
    for(unsigned i=0;i<12u;++i)state.particles[i]=(PortableSnowWeatherParticle){
        .x_q16=100*65536,.y_q16=20*65536,.active=1,.flags=1,.stage=(int16_t)(i%4u)};
    in.parity=1;
    SNOW_CHECK(portable_snow_weather_tick(&state,&rules,&in,sine,1024,snow_random,&rng)==1 && !rng.calls);
    unsigned visible=0;
    for(unsigned i=0;i<12u;++i){const unsigned depth=i%4u;
        SNOW_CHECK(state.particles[i].phase==(depth<2u?256u:(depth==2u?320u:384u)));
        SNOW_CHECK(state.particles[i].y_q16==20*65536+(depth<2u?0x4800:(depth==2u?0x9000:0x10000)));
        visible+=state.snapshot.particles[i].visible;
        SNOW_CHECK(state.snapshot.particles[i].visible==(i&1u));}
    SNOW_CHECK(visible==6);

    /* Draw metadata retains fractions and byte wrapping without changing
     * source particle integration, accepted visibility, digest or RNG. */
    portable_snow_weather_init(&state, &rules, 0, 0, 16384, 1);
    snow_wait(&state);
    state.particles[0] = (PortableSnowWeatherParticle){.active = 1,
                                                       .flags = 1,
                                                       .x_q16 = 315 * 65536 + 16384,
                                                       .y_q16 = -4 * 65536 + 16384,
                                                       .stage = 2};
    const uint64_t before_snapshot = portable_snow_weather_digest(&state);
    portable_snow_weather_snapshot(&state, &rules, 0);
    SNOW_CHECK(state.snapshot.precise && state.snapshot.particles[0].active &&
               state.snapshot.particles[0].x == 59 && state.snapshot.particles[0].y == 252 &&
               state.snapshot.particles[0].x_q16 == 59 * 65536 + 16384 &&
               state.snapshot.particles[0].y_q16 == -4 * 65536 + 16384 &&
               portable_snow_weather_digest(&state) == before_snapshot);
    state.particles[0] = (PortableSnowWeatherParticle){
        .active = 1, .flags = 1, .x_q16 = 100 * 65536, .y_q16 = 20 * 65536, .stage = 0};
    rng = (SnowRandom){0};
    in = (PortableSnowWeatherInputs){.yaw = 16400};
    SNOW_CHECK(portable_snow_weather_tick(&state, &rules, &in, sine, 1024, snow_random, &rng) == 1);
    SNOW_CHECK(!rng.calls && state.particles[0].x_q16 == 100 * 65536 - 16384 &&
               state.snapshot.particles[0].x == 99 &&
               state.snapshot.particles[0].x_q16 == 100 * 65536 - 16384 &&
               state.snapshot.particles[0].y_q16 == 20 * 65536 + 0x4800);
    in = (PortableSnowWeatherInputs){.yaw = 16384, .parity = 1};

    /* Both offscreen branches consume RNG, in X then Y order, with Y winning. */
    portable_snow_weather_init(&state, &rules, 0, 0, 16384, 1);
    snow_wait(&state);
    state.particles[0] =
        (PortableSnowWeatherParticle){.active = 1, .flags = 3, .x_q16 = -1, .y_q16 = 85 * 65536};
    rng=(SnowRandom){.values={31,63}};
    SNOW_CHECK(portable_snow_weather_tick(&state,&rules,&in,sine,1024,snow_random,&rng)==1);
    SNOW_CHECK(rng.calls==2 && !state.particles[0].active && state.particles[0].flags==2 &&
        state.particles[0].phase==83 && !state.snapshot.particles[0].visible);

    /* Last two slots defer spawning while camera translates; each consumes one. */
    portable_snow_weather_init(&state,&rules,0,0,16384,1);snow_wait(&state);
    state.particles[10].phase=state.particles[11].phase=0;
    sine[256]=65536;in.camera_x_q16=32768;rng=(SnowRandom){.values={9,17}};
    SNOW_CHECK(portable_snow_weather_tick(&state,&rules,&in,sine,1024,snow_random,&rng)==1 && rng.calls==2);
    SNOW_CHECK(!state.particles[10].active && state.particles[10].phase==9 && state.particles[11].phase==17);
    SNOW_CHECK(state.prior_camera_x_q16==32768 && state.prior_yaw==16384);

    /* Exact stage thresholds and the source's double stage increment at depth2. */
    portable_snow_weather_init(&state,&rules,0,0,16384,1);snow_wait(&state);sine[256]=1;
    state.particles[0]=(PortableSnowWeatherParticle){.active=1,.flags=1,.x_q16=120*65536,
        .travel_q16=72*65536-1};
    state.particles[1]=(PortableSnowWeatherParticle){.active=1,.flags=1,.stage=2,
        .x_q16=120*65536,.y_q16=71*65536,.travel_q16=80*65536-1};
    rng=(SnowRandom){0};
    SNOW_CHECK(portable_snow_weather_tick(&state,&rules,&in,sine,1024,snow_random,&rng)==1 && !rng.calls);
    SNOW_CHECK(state.particles[0].stage==1 && !state.particles[0].travel_q16 &&
        state.particles[0].y_q16==0x3000);
    SNOW_CHECK(state.particles[1].stage==4 && !state.particles[1].travel_q16 &&
        state.particles[1].y_q16==71*65536+0x6800);

    /* Signed halfword phase wraps, but division to sine slot truncates to zero. */
    memset(sine,0,sizeof sine);sine[516]=10;sine[511]=3;in.camera_x_q16=0;
    portable_snow_weather_init(&state,&rules,0,0,16384,1);snow_wait(&state);
    state.particles[0]=(PortableSnowWeatherParticle){.active=1,.flags=1,.phase=32767,.x_q16=100*65536};
    SNOW_CHECK(portable_snow_weather_tick(&state,&rules,&in,sine,1024,snow_random,&rng)==1);
    SNOW_CHECK(state.particles[0].phase==33023 && state.particles[0].x_q16==100*65536+112);

    /* Source signed-yaw subtraction deliberately does not take shortest arc. */
    memset(sine,0,sizeof sine);portable_snow_weather_init(&state,&rules,0,0,32767,1);snow_wait(&state);
    state.particles[0]=(PortableSnowWeatherParticle){.active=1,.flags=1,.x_q16=120*65536};
    in.yaw=32768;rng=(SnowRandom){.values={5}};
    SNOW_CHECK(portable_snow_weather_tick(&state,&rules,&in,sine,1024,snow_random,&rng)==1 && rng.calls==1);
    SNOW_CHECK(state.particles[0].x_q16==120*65536+65535*1024 && !state.particles[0].active);

    /* Negative subpixel camera deltas truncate before multiplication. Negative
     * radial Y products use arithmetic shift, unlike the radial X divisions. */
    in=(PortableSnowWeatherInputs){.yaw=16384,.camera_x_q16=-32767};
    sine[256]=1;portable_snow_weather_init(&state,&rules,0,0,16384,1);snow_wait(&state);
    state.particles[0]=(PortableSnowWeatherParticle){.active=1,.flags=1,.x_q16=120*65536};
    rng=(SnowRandom){0};
    SNOW_CHECK(portable_snow_weather_tick(&state,&rules,&in,sine,1024,snow_random,&rng)==1 &&
        !state.particles[0].travel_q16 && state.particles[0].y_q16==0x4800);
    portable_snow_weather_init(&state,&rules,0,0,16384,1);snow_wait(&state);
    state.particles[0]=(PortableSnowWeatherParticle){.active=1,.flags=1,.stage=2,
        .x_q16=120*65536,.y_q16=81*65536};in.camera_x_q16=-32768;
    SNOW_CHECK(portable_snow_weather_tick(&state,&rules,&in,sine,1024,snow_random,&rng)==1);
    SNOW_CHECK(state.particles[0].stage==3 && state.particles[0].y_q16==81*65536+0x9000-1);

    /* Choice direction and the four-call spawn order remain observable. */
    for(unsigned stage=0;stage<4u;++stage){
        rules.backward[0]=(uint8_t)stage;
        portable_snow_weather_init(&state,&rules,0,0,16384,1);snow_wait(&state);state.particles[0].phase=0;
        rng=(SnowRandom){.values={0,456,2,31}};
        SNOW_CHECK(portable_snow_weather_tick(&state,&rules,&in,sine,1024,snow_random,&rng)==1 && rng.calls==4);
        SNOW_CHECK(state.particles[0].stage==(int)stage && state.particles[0].phase==456 &&
            state.particles[0].x_q16==10*65536 &&
            state.particles[0].y_q16==(stage<2u?39:(stage==2u?23:7))*65536);
    }
    /* Ordinary original Star Cup / Snow Land GP, 3670C entry/return at
     * source scene clock457: held countdown camera, main RNG199148118 unchanged.
     * Synthetic sine differences reproduce the observed wobble, not ROM tables. */
    memset(sine,0,sizeof sine);sine[912]=616;
    portable_snow_weather_init(&state,&rules,11796480,87293952,0,1);snow_wait(&state);
    state.particles[0]=(PortableSnowWeatherParticle){.x_q16=4792176,.y_q16=4900864,
        .stage=1,.active=1,.phase=58078,.flags=1,.travel_q16=98271};
    in=(PortableSnowWeatherInputs){.camera_x_q16=11796480,.camera_y_q16=87293952,.yaw=0};
    rng=(SnowRandom){.lcg=1,.state=199148118};
    SNOW_CHECK(portable_snow_weather_tick(&state,&rules,&in,sine,1024,snow_random,&rng)==1);
    SNOW_CHECK(state.particles[0].x_q16==4802032 && state.particles[0].y_q16==4919296 &&
        state.particles[0].phase==58334 && state.particles[0].travel_q16==98271 &&
        !rng.calls && rng.state==199148118);

    /* Same ordinary countdown, clock459: slot3 respawns with four calls of
     * actual main RNG; stage1, phase22925, X280, Y36, final RNG2761731554. */
    portable_snow_weather_init(&state,&rules,11796480,87293952,0,1);snow_wait(&state);
    rules.forward[3]=1;state.particles[3].phase=0;
    rng=(SnowRandom){.lcg=1,.state=199148118};
    SNOW_CHECK(portable_snow_weather_tick(&state,&rules,&in,sine,1024,snow_random,&rng)==1);
    SNOW_CHECK(rng.calls==4 && rng.state==UINT32_C(2761731554) &&
        state.particles[3].stage==1 && state.particles[3].phase==22925 &&
        state.particles[3].x_q16==18350080 && state.particles[3].y_q16==2359296 &&
        state.particles[3].flags==1 && state.particles[3].active==1);

    /* Ordinary A-driving source clock817: prior accepted Y77090816 becomes
     * Y76898048, yaw0, hence dy=-5 and forward163840 using the orthogonal
     * sine basis. Isolate captured slot1; no other owners consume RNG here. */
    memset(sine,0,sizeof sine);sine[768]=-32768;sine[791]=103;
    portable_snow_weather_init(&state,&rules,11796480,77090816,0,1);snow_wait(&state);
    state.particles[1]=(PortableSnowWeatherParticle){.x_q16=594800,.y_q16=4253696,
        .stage=1,.active=1,.phase=50354,.flags=1,.travel_q16=2686976};
    in=(PortableSnowWeatherInputs){.camera_x_q16=11796480,.camera_y_q16=76898048,.yaw=0};
    rng=(SnowRandom){.lcg=1,.state=474468725};
    SNOW_CHECK(portable_snow_weather_tick(&state,&rules,&in,sine,1024,snow_random,&rng)==1);
    SNOW_CHECK(state.particles[1].x_q16==595624 && state.particles[1].y_q16==4265984 &&
        state.particles[1].phase==50610 && state.particles[1].travel_q16==2850816 &&
        state.prior_camera_y_q16==76898048 && !rng.calls && rng.state==474468725);

    /* Restart and repeated fixed-tick runs reproduce every typed field/RNG draw. */
    PortableSnowWeatherState a,b;SnowRandom ra={.lcg=1,.state=7},rb=ra;
    portable_snow_weather_init(&a,&rules,0,0,16384,1);portable_snow_weather_init(&b,&rules,0,0,16384,1);
    for(unsigned tick=0;tick<200u;++tick){
        in=(PortableSnowWeatherInputs){.yaw=16384,.parity=(uint8_t)tick,.camera_x_q16=(int32_t)tick*32768,
            .paused=(uint8_t)(tick>=40u && tick<50u)};
        SNOW_CHECK(portable_snow_weather_tick(&a,&rules,&in,sine,1024,snow_random,&ra)==1);
        SNOW_CHECK(portable_snow_weather_tick(&b,&rules,&in,sine,1024,snow_random,&rb)==1);
        SNOW_CHECK(portable_snow_weather_digest(&a)==portable_snow_weather_digest(&b) &&
            memcmp(&a,&b,sizeof a)==0 && ra.state==rb.state && ra.calls==rb.calls);
    }
    b=a;b.particles[11].phase^=1;
    SNOW_CHECK(portable_snow_weather_digest(&a)!=portable_snow_weather_digest(&b));
    puts("portable snow weather: PASS");
    return 0;
}
