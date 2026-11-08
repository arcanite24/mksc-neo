#include "race_rumble.h"

#include <stddef.h>

static PortableRumbleEvent stronger(PortableRumbleEvent a,PortableRumbleEvent b)
{
    return b>a?b:a;
}

/* Contact cues name the B110/B2B4/actor contact that just happened. */
static PortableRumbleEvent contact_event(uint16_t song)
{
    switch(song){
    case 125u:return PORTABLE_RUMBLE_BOOST;          /* mushroom/dash ground boost */
    case 160u:return PORTABLE_RUMBLE_ITEM_HIT;       /* damaging course actor */
    case 133u:                                        /* un-flatten */
    case 162u:                                        /* spin expiry or Piranha swallow */
    case 165u:return PORTABLE_RUMBLE_NONE;           /* spin brake-out */
    default:return PORTABLE_RUMBLE_BUMP;             /* walls, karts, props, launches */
    }
}

static int queued_hit(uint8_t status) { return status==5u||status==6u||status==13u; }
static int surface_spin(uint8_t status) { return status==7u||status==8u; }

PortableRumbleEvent portable_rumble_step(PortableRumbleTracker *tracker,
    const PortableRumbleView *view)
{
    if(tracker==NULL||view==NULL||view->engine==NULL||view->contact==NULL||view->boost_voice==NULL)
        return PORTABLE_RUMBLE_NONE;
    const PortableDriverEngineSoundInput *engine=view->engine;
    PortableRumbleEvent event=PORTABLE_RUMBLE_NONE;
    if(tracker->primed){
        if(view->contact->serial!=tracker->contact_serial)
            event=stronger(event,contact_event(view->contact->song_id));
        if(view->boost_voice->serial!=tracker->boost_serial)
            event=stronger(event,PORTABLE_RUMBLE_BOOST);
        if(queued_hit(engine->response_status)&&!queued_hit(tracker->response_status))
            event=stronger(event,PORTABLE_RUMBLE_ITEM_HIT);
        if(surface_spin(engine->response_status)&&!surface_spin(tracker->response_status))
            event=stronger(event,PORTABLE_RUMBLE_SPIN_OUT);
        if(engine->flattened&&!tracker->flattened)event=stronger(event,PORTABLE_RUMBLE_ITEM_HIT);
        if(engine->lightning_timer>0&&tracker->lightning_timer<=0)
            event=stronger(event,PORTABLE_RUMBLE_ITEM_HIT);
        if(engine->hazard_audio_kind!=0u)event=stronger(event,PORTABLE_RUMBLE_HAZARD);
        /* Only landings from a ramp/surface jump rumble, not every hop. */
        if(engine->landed&&tracker->jumping)event=stronger(event,PORTABLE_RUMBLE_LANDING);
    }
    if(engine->surface_jump_started)tracker->jumping=1u;
    else if(engine->landed||engine->hazard_audio_kind!=0u)tracker->jumping=0u;
    tracker->contact_serial=view->contact->serial;
    tracker->boost_serial=view->boost_voice->serial;
    tracker->response_status=engine->response_status;
    tracker->flattened=engine->flattened;
    tracker->lightning_timer=engine->lightning_timer;
    tracker->primed=1u;
    return event;
}

PortableRumblePulse portable_rumble_pulse(PortableRumbleEvent event)
{
    static const PortableRumblePulse pulses[PORTABLE_RUMBLE_EVENT_COUNT]={
        {0u,0u,0u},
        {0x2000u,0x6000u,180u},  /* boost: light, high-frequency buzz */
        {0x6000u,0x2000u,90u},   /* landing: short thump */
        {0x5000u,0x3000u,110u},  /* bump */
        {0x9000u,0x5000u,350u},  /* spin-out */
        {0xc000u,0x8000u,400u},  /* item or actor hit, crush, lightning */
        {0xffffu,0x9000u,500u},  /* fall or lava */
    };
    return (unsigned)event<PORTABLE_RUMBLE_EVENT_COUNT?pulses[event]:pulses[0];
}
