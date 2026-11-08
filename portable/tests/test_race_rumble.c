#include "race_rumble.h"

#include <stdio.h>

#define CHECK(condition) do { if(!(condition)){ \
    fprintf(stderr,"race rumble failed line %d: %s\n",__LINE__,#condition);return 1;} } while(0)

typedef struct {
    PortableDriverEngineSoundInput engine;
    PortableDriverContactAudio contact,boost;
    PortableRumbleView view;
} Driver;

static void reset(Driver *d)
{
    *d=(Driver){0};
    d->view=(PortableRumbleView){&d->engine,&d->contact,&d->boost};
}

static void cue(PortableDriverContactAudio *audio,uint16_t song)
{
    ++audio->serial;audio->song_id=song;
}

int main(void)
{
    Driver d;PortableRumbleTracker t={0};
    reset(&d);
    /* The first view primes: stale serials, spins or hazards never rumble. */
    d.contact.serial=40u;d.engine.response_status=5u;d.engine.hazard_audio_kind=1u;
    CHECK(portable_rumble_step(&t,&d.view)==PORTABLE_RUMBLE_NONE);
    d.engine.hazard_audio_kind=0u;
    CHECK(portable_rumble_step(&t,&d.view)==PORTABLE_RUMBLE_NONE);
    d.engine.response_status=0u;
    CHECK(portable_rumble_step(&t,&d.view)==PORTABLE_RUMBLE_NONE);

    /* Contact cues: walls/karts bump, 125 boosts, spin-recovery cues are quiet. */
    cue(&d.contact,99u);CHECK(portable_rumble_step(&t,&d.view)==PORTABLE_RUMBLE_BUMP);
    CHECK(portable_rumble_step(&t,&d.view)==PORTABLE_RUMBLE_NONE);
    cue(&d.contact,125u);cue(&d.boost,300u);
    CHECK(portable_rumble_step(&t,&d.view)==PORTABLE_RUMBLE_BOOST);
    cue(&d.contact,165u);CHECK(portable_rumble_step(&t,&d.view)==PORTABLE_RUMBLE_NONE);
    cue(&d.contact,162u);CHECK(portable_rumble_step(&t,&d.view)==PORTABLE_RUMBLE_NONE);
    cue(&d.contact,160u);CHECK(portable_rumble_step(&t,&d.view)==PORTABLE_RUMBLE_ITEM_HIT);

    /* A9AC queued hits (shell/banana: status 5/6, strong spin 13) and
     * surface spin-outs (7/8) rumble once on entry. */
    d.engine.response_status=6u;CHECK(portable_rumble_step(&t,&d.view)==PORTABLE_RUMBLE_ITEM_HIT);
    CHECK(portable_rumble_step(&t,&d.view)==PORTABLE_RUMBLE_NONE);
    d.engine.response_status=0u;CHECK(portable_rumble_step(&t,&d.view)==PORTABLE_RUMBLE_NONE);
    d.engine.response_status=13u;CHECK(portable_rumble_step(&t,&d.view)==PORTABLE_RUMBLE_ITEM_HIT);
    d.engine.response_status=7u;CHECK(portable_rumble_step(&t,&d.view)==PORTABLE_RUMBLE_SPIN_OUT);
    d.engine.response_status=8u;CHECK(portable_rumble_step(&t,&d.view)==PORTABLE_RUMBLE_NONE);
    d.engine.response_status=0u;CHECK(portable_rumble_step(&t,&d.view)==PORTABLE_RUMBLE_NONE);

    /* Crush and lightning shrink are hits; a wall bump in the same tick yields to them. */
    d.engine.flattened=1u;cue(&d.contact,99u);
    CHECK(portable_rumble_step(&t,&d.view)==PORTABLE_RUMBLE_ITEM_HIT);
    d.engine.flattened=0u;CHECK(portable_rumble_step(&t,&d.view)==PORTABLE_RUMBLE_NONE);
    d.engine.lightning_timer=360;CHECK(portable_rumble_step(&t,&d.view)==PORTABLE_RUMBLE_ITEM_HIT);
    d.engine.lightning_timer=359;CHECK(portable_rumble_step(&t,&d.view)==PORTABLE_RUMBLE_NONE);
    d.engine.lightning_timer=0;CHECK(portable_rumble_step(&t,&d.view)==PORTABLE_RUMBLE_NONE);

    /* Ramp landings rumble; ordinary hop landings do not. */
    d.engine.landed=1u;CHECK(portable_rumble_step(&t,&d.view)==PORTABLE_RUMBLE_NONE);
    d.engine.landed=0u;d.engine.surface_jump_started=1u;
    CHECK(portable_rumble_step(&t,&d.view)==PORTABLE_RUMBLE_NONE);
    d.engine.surface_jump_started=0u;CHECK(portable_rumble_step(&t,&d.view)==PORTABLE_RUMBLE_NONE);
    d.engine.landed=1u;CHECK(portable_rumble_step(&t,&d.view)==PORTABLE_RUMBLE_LANDING);
    CHECK(portable_rumble_step(&t,&d.view)==PORTABLE_RUMBLE_NONE);
    d.engine.landed=0u;

    /* Falls and lava outrank everything else in the tick. */
    d.engine.hazard_audio_kind=1u;d.engine.response_status=7u;cue(&d.contact,125u);
    CHECK(portable_rumble_step(&t,&d.view)==PORTABLE_RUMBLE_HAZARD);

    /* Pulses grow with severity and stay within SDL's 16-bit motor range. */
    for(int e=PORTABLE_RUMBLE_BOOST;e<PORTABLE_RUMBLE_EVENT_COUNT;++e){
        const PortableRumblePulse p=portable_rumble_pulse((PortableRumbleEvent)e);
        CHECK(p.duration_ms>0u&&p.duration_ms<=500u&&p.low>0u);
    }
    CHECK(portable_rumble_pulse(PORTABLE_RUMBLE_NONE).duration_ms==0u);
    CHECK(portable_rumble_pulse(PORTABLE_RUMBLE_HAZARD).low>portable_rumble_pulse(PORTABLE_RUMBLE_BUMP).low);
    CHECK(portable_rumble_pulse(PORTABLE_RUMBLE_ITEM_HIT).duration_ms>portable_rumble_pulse(PORTABLE_RUMBLE_LANDING).duration_ms);
    CHECK(portable_rumble_step(NULL,&d.view)==PORTABLE_RUMBLE_NONE);
    puts("race rumble: PASS presentation events map to bounded controller pulses");
    return 0;
}
