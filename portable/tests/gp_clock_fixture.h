#ifndef PORTABLE_TEST_GP_CLOCK_FIXTURE_H
#define PORTABLE_TEST_GP_CLOCK_FIXTURE_H
#include "gp_race_session.h"

/* Explicit test content for the historical timing assertions. Production
 * sessions receive the clock owner from their composed HUD asset. */
static inline const PortableHudAssetView *test_gp_clock_hud(void)
{
    static uint8_t steps[PORTABLE_HUD_CLOCK_STEP_COUNT];
    static PortableHudAssetView hud;
    for(size_t i=0u;i<PORTABLE_HUD_CLOCK_STEP_COUNT;++i)
        steps[i]=(uint8_t)(i%3u==0u?1u:2u);
    hud.clock_steps=steps;hud.clock_step_count=PORTABLE_HUD_CLOCK_STEP_COUNT;
    return &hud;
}
static inline PortableGpRaceSessionStatus test_gp_clock_attach(
    PortableGpRaceSessionStatus status,PortableGpRaceSession *session)
{
    if(status==PORTABLE_GP_RACE_SESSION_OK){
        const PortableHudAssetView *hud=test_gp_clock_hud();
        session->hud_clock_steps=hud->clock_steps;
        session->hud_clock_step_count=hud->clock_step_count;
    }
    return status;
}
static inline PortableGpRaceSessionStatus test_gp_init(
    PortableGpRaceSession *s,const PortableRecoveredRaceView *r,uint8_t driver)
{return test_gp_clock_attach(portable_gp_race_session_init(s,r,driver),s);}
static inline PortableGpRaceSessionStatus test_gp_init_grid(
    PortableGpRaceSession *s,const PortableRecoveredRaceView *r,uint8_t driver,const uint8_t ranks[8])
{return test_gp_clock_attach(portable_gp_race_session_init_grid_order(s,r,driver,ranks),s);}
static inline PortableGpRaceSessionStatus test_gp_init_engine_grid(
    PortableGpRaceSession *s,const PortableRecoveredRaceView *r,uint8_t driver,
    const uint8_t ranks[8],uint8_t engine)
{return test_gp_clock_attach(portable_gp_race_session_init_engine_grid_order(s,r,driver,ranks,engine),s);}
static inline PortableGpRaceSessionStatus test_gp_init_quick(
    PortableGpRaceSession *s,const PortableRecoveredRaceView *r,uint8_t driver,
    uint8_t engine,PortableQuickRunOptions options)
{return test_gp_clock_attach(portable_gp_race_session_init_quick_run(s,r,driver,engine,options),s);}
static inline PortableGpRaceSessionStatus test_gp_init_locals(
    PortableGpRaceSession *s,const PortableRecoveredRaceView *const r[],
    const uint8_t drivers[],uint8_t count,uint8_t engine,PortableQuickRunOptions options)
{return test_gp_clock_attach(portable_gp_race_session_init_locals(s,r,drivers,count,engine,options),s);}

#endif
