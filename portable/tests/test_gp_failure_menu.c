#include "gp_failure_menu.h"

#include <stdio.h>

#define CHECK(value) do { if (!(value)) { \
    fprintf(stderr, "GP failure menu failed at line %d\n", __LINE__); \
    return 1; \
} } while (0)

static int open_choice(PortableGpFailureMenu *menu)
{
    for(unsigned tick=0u;tick<6u;++tick)CHECK(portable_gp_failure_menu_tick(menu,9u));
    CHECK(menu->phase==PORTABLE_GP_FAILURE_MENU_CHOOSE && menu->choice==0u && !menu->cue);
    return 0;
}

int main(void)
{
    PortableGpFailureMenu menu;
    CHECK(portable_gp_failure_menu_init(&menu,3u) && menu.phase==PORTABLE_GP_FAILURE_MENU_OPEN);
    CHECK(open_choice(&menu)==0);
    /* gKeyTriggerBuf edges toggle; holding without a new edge cannot repeat. */
    CHECK(portable_gp_failure_menu_tick(&menu,0x80u) && menu.choice==1u && menu.cue==141u);
    for(unsigned i=0u;i<30u;++i)CHECK(portable_gp_failure_menu_tick(&menu,0u) && menu.choice==1u && !menu.cue);
    CHECK(portable_gp_failure_menu_tick(&menu,0x80u) && !menu.choice && menu.cue==141u);
    CHECK(portable_gp_failure_menu_tick(&menu,0x40u) && menu.choice==1u && menu.cue==141u);
    CHECK(portable_gp_failure_menu_tick(&menu,1u) && menu.phase==PORTABLE_GP_FAILURE_MENU_DISPATCH &&
        !menu.cue && menu.retry_credits==3u && !menu.action);
    CHECK(portable_gp_failure_menu_tick(&menu,0u) && menu.phase==PORTABLE_GP_FAILURE_MENU_QUIT_OPEN &&
        !menu.phase_ticks && menu.cue==142u);
    for(unsigned tick=1u;tick<8u;++tick)CHECK(portable_gp_failure_menu_tick(&menu,0u) &&
        menu.phase==PORTABLE_GP_FAILURE_MENU_QUIT_OPEN && menu.phase_ticks==tick);
    CHECK(portable_gp_failure_menu_tick(&menu,0u) && menu.phase==PORTABLE_GP_FAILURE_MENU_CONFIRM_QUIT && !menu.confirm_yes);
    CHECK(portable_gp_failure_menu_tick(&menu,0x10u) && menu.confirm_yes && menu.cue==141u);
    CHECK(portable_gp_failure_menu_tick(&menu,0x10u) && !menu.confirm_yes && menu.cue==141u);
    CHECK(portable_gp_failure_menu_tick(&menu,1u) && menu.phase==PORTABLE_GP_FAILURE_MENU_QUIT_NO_RETURN && menu.cue==142u);
    for(unsigned i=0u;i<7u;++i)CHECK(portable_gp_failure_menu_tick(&menu,0u));
    CHECK(menu.phase==PORTABLE_GP_FAILURE_MENU_CHOOSE && menu.choice==1u); /* source37 keeps QUIT */
    CHECK(portable_gp_failure_menu_tick(&menu,9u) && menu.phase==PORTABLE_GP_FAILURE_MENU_DISPATCH);
    CHECK(portable_gp_failure_menu_tick(&menu,0u));
    for(unsigned i=0u;i<8u;++i)CHECK(portable_gp_failure_menu_tick(&menu,0u));
    CHECK(menu.phase==PORTABLE_GP_FAILURE_MENU_CONFIRM_QUIT);
    CHECK(portable_gp_failure_menu_tick(&menu,0x20u) && menu.confirm_yes);
    CHECK(portable_gp_failure_menu_tick(&menu,1u) && menu.phase==PORTABLE_GP_FAILURE_MENU_QUIT_YES_START &&
        menu.cue==143u && menu.action==PORTABLE_GP_FAILURE_ACTION_QUIT);
    CHECK(portable_gp_failure_menu_tick(&menu,0u) && menu.phase==PORTABLE_GP_FAILURE_MENU_QUIT_SHUTTER && !menu.phase_ticks);
    for(unsigned i=0u;i<64u;++i)CHECK(portable_gp_failure_menu_tick(&menu,0u));
    CHECK(menu.phase==PORTABLE_GP_FAILURE_MENU_COMPLETE);

    CHECK(portable_gp_failure_menu_init(&menu,3u) && open_choice(&menu)==0);
    CHECK(portable_gp_failure_menu_tick(&menu,1u) && menu.phase==PORTABLE_GP_FAILURE_MENU_DISPATCH &&
        !menu.cue && menu.retry_credits==3u);
    CHECK(portable_gp_failure_menu_tick(&menu,0u) && menu.phase==PORTABLE_GP_FAILURE_MENU_RETRY_HOLD &&
        menu.cue==143u && menu.retry_credits==2u && !menu.phase_ticks && menu.action==PORTABLE_GP_FAILURE_ACTION_RETRY);
    /* Observed source A+2 counter0, A+50 counter48, A+51 finishes the owner. */
    for(unsigned tick=1u;tick<=48u;++tick)CHECK(portable_gp_failure_menu_tick(&menu,0u) &&
        menu.phase==PORTABLE_GP_FAILURE_MENU_RETRY_HOLD && menu.phase_ticks==tick && !menu.cue);
    CHECK(portable_gp_failure_menu_tick(&menu,0u) && menu.phase==PORTABLE_GP_FAILURE_MENU_RETRY_SHUTTER && !menu.phase_ticks);
    for(unsigned i=0u;i<64u;++i)CHECK(portable_gp_failure_menu_tick(&menu,0u));
    CHECK(menu.phase==PORTABLE_GP_FAILURE_MENU_COMPLETE && menu.retry_credits==2u);
    CHECK(!portable_gp_failure_menu_init(&menu,4u));
    CHECK(portable_gp_failure_menu_init(&menu,0u) && menu.phase==PORTABLE_GP_FAILURE_MENU_GAME_OVER_SETUP);
    for(unsigned i=0u;i<3u;++i)CHECK(portable_gp_failure_menu_tick(&menu,9u));
    CHECK(menu.phase==PORTABLE_GP_FAILURE_MENU_GAME_OVER && !menu.phase_ticks);
    for(unsigned i=0u;i<60u;++i)CHECK(portable_gp_failure_menu_tick(&menu,9u) && menu.phase==PORTABLE_GP_FAILURE_MENU_GAME_OVER);
    CHECK(portable_gp_failure_menu_tick(&menu,8u) && menu.phase==PORTABLE_GP_FAILURE_MENU_QUIT_YES_START && menu.cue==143u);
    CHECK(portable_gp_failure_menu_tick(&menu,0u) && menu.phase==PORTABLE_GP_FAILURE_MENU_QUIT_SHUTTER);
    for(unsigned i=0u;i<64u;++i)CHECK(portable_gp_failure_menu_tick(&menu,0u));
    CHECK(menu.phase==PORTABLE_GP_FAILURE_MENU_COMPLETE);
    puts("portable GP failure menu: PASS");
    return 0;
}
