#include "startup_trace.h"
#include "mesh_time_trial_cli.h"
#include "replay_file.h"
#ifdef PORTABLE_ENABLE_SDL3
#include <SDL3/SDL_main.h>
#include "startup_pack.h"
#include "loading_screen.h"
#include "mobile_lifecycle.h"
#endif
#include "gp_failure_menu.h"
#include "game_host_internal.h"
#include "content_import.h"
#include "rom_import_host.h"
#include "game_import_host.h"
#include "first_run.h"
#include "mod_loader.h"
#include "driving_profile.h"
#include "text_number.h"
#if defined(_WIN32) && defined(PORTABLE_WINDOWS_GUI)
#include <windows.h>
#endif

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    long length;
    uint8_t *bytes;
    if (file == NULL || fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) <= 0 ||
        (uint64_t)length > (uint64_t)INT32_MAX || fseek(file, 0, SEEK_SET) != 0)
    {
        if (file)
            fclose(file);
        return NULL;
    }
    bytes = malloc((size_t)length);
    if (bytes == NULL || fread(bytes, 1, (size_t)length, file) != (size_t)length)
    {
        free(bytes);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return bytes;
}

static int parse_id(const char *text, uint32_t *out)
{
    const char *at = text;
    const char *end = text + strlen(text);
    uint64_t value;
    if (!portable_text_u64(&at, end, 0u, UINT32_MAX, &value) || at != end || value == 0u)
        return 0;
    *out = (uint32_t)value;
    return 1;
}

static int run_time_trial_replay(const PortableAssetPackView *pack, const char *replay_path,
                                 uint32_t course_id, uint32_t character_id, int require_finished)
{
    PortableContentSelection selection;
    PortableRecoveredPackStorage *storage = malloc(sizeof *storage);
    PortableRecoveredRaceView race;
    PortableRecoveredTimeTrialSession *session = calloc(1u, sizeof *session);
    PortableRecoveredTimeTrialSnapshot *snapshot = calloc(1u, sizeof *snapshot);
    FILE *replay = NULL;
    size_t ticks = 0u;
    int ok = 0;

    if (storage == NULL || session == NULL || snapshot == NULL)
        goto done;
    if (portable_content_registry_select(
            pack, course_id, character_id, &selection) != PORTABLE_CONTENT_OK ||
        portable_course_open_race(
            pack, &selection, storage, &race, portable_mod_identity_mirror()) != PORTABLE_RECOVERED_PACK_OK ||
        portable_recovered_time_trial_init(session, &race) !=
            PORTABLE_RECOVERED_TIME_TRIAL_OK)
        goto done;
    replay = fopen(replay_path, "r");
    if (replay == NULL)
    {
        fprintf(stderr, "could not read Time Trial replay: %s\n", replay_path);
        goto done;
    }
    ok = portable_recovered_replay_run_file(replay, session, stderr, &ticks) &&
        portable_recovered_time_trial_snapshot(session, snapshot) ==
            PORTABLE_RECOVERED_TIME_TRIAL_OK &&
        (!require_finished ||
         (snapshot->phase == PORTABLE_RACE_PHASE_FINISHED &&
          snapshot->laps_completed == snapshot->lap_count));
    if (ok)
        printf("recovered Time Trial replay: PASS %zu tick(s) phase=%u "
               "laps=%u/%u rescues=%u result=%u ghost_recording=%s\n",
               ticks, snapshot->phase, snapshot->laps_completed,
               snapshot->lap_count, snapshot->rescue_count,
               snapshot->total_ticks,session->ghost_recording_valid?"available":
                   session->ghost_encoder.exhausted?"exhausted":"invalid");
    else if (require_finished)
        fprintf(stderr, "recovered Time Trial replay did not finish\n");
done:
    if (replay != NULL)
        fclose(replay);
    free(snapshot);
    free(session);
    free(storage);
    return ok;
}

static int run_gp_smoke(const PortableAssetPackView *pack,
    uint32_t course_id,uint32_t character_id,int require_all,uint8_t engine_class,
    const PortableGpCupSession *previous,const PortableGpCupDefinition *cup,
    uint8_t track,PortableGpCupSession *standings,uint8_t retry_credits)
{
    unsigned retained_red_ticks=0u;
    unsigned cpu_banana_ticks=0u,cpu_banana_flight_ticks=0u,cpu_green_ticks=0u,cpu_red_ticks=0u,cpu_spiny_ticks=0u;
    unsigned retained_banana_ticks=0u;
    unsigned star_ticks=0u, mushroom_ticks=0u, green_ticks=0u, red_ticks=0u, banana_ticks=0u, banana_flight_ticks=0u;
    PortableContentSelection selection;
    PortableRecoveredPackStorage *storage=malloc(sizeof *storage);
    PortableGpRaceSession *session=calloc(1,sizeof *session);
    PortablePresentationPackStorage *presentation_storage=malloc(sizeof *presentation_storage);
    PortablePresentationPackView presentation;
    PortableRecoveredRaceView race;
    uint32_t ticks=0u;
    unsigned finished_count=0u;
    int ok=0;
    PortableGpRaceSessionStatus status=PORTABLE_GP_RACE_SESSION_INVALID_ARGUMENT;
    if (storage==NULL || session==NULL || presentation_storage==NULL ||
        portable_content_registry_select(pack,course_id,character_id,&selection)!=PORTABLE_CONTENT_OK ||
        portable_course_open_race(pack,&selection,storage,&race,portable_mod_identity_mirror())!=PORTABLE_RECOVERED_PACK_OK ||
        portable_course_open_presentation(pack,&selection,storage,&race,presentation_storage,
            &presentation,portable_mod_identity_mirror())!=PORTABLE_RECOVERED_PACK_OK)
    {
        fprintf(stderr,"GP smoke selection failed course=%u character=%u\n",course_id,character_id);
        goto done;
    }
    uint8_t ranks[8]={0};
    if(previous!=NULL)
        for(uint8_t rank=0u;rank<8u;++rank)ranks[previous->race_order[rank]]=rank;
    status=portable_gp_race_session_init_engine_grid_order(session,&race,
        (uint8_t)selection.character.character_class,previous!=NULL?ranks:NULL,engine_class);
    if (status!=PORTABLE_GP_RACE_SESSION_OK) goto report;
    /* The headless race uses the same composed source clock as rendered GP,
     * without enabling visual rank/coin HUD owners in its saved snapshots. */
    status=portable_gp_race_session_configure_hud(session,NULL,&presentation.hud);
    if(status!=PORTABLE_GP_RACE_SESSION_OK){
        fprintf(stderr,"GP smoke course=%u: missing HUD clock owner; reimport your ROM.\n",course_id);
        goto report;
    }
    session->retry_credits=retry_credits;
    const uint8_t points[8]={0};
    if (portable_gp_race_roster_apply_cup_ai_setup(&session->roster,previous!=NULL?previous->cumulative_points:points,track,engine_class)!=PORTABLE_GP_ROSTER_OK)
    { status=PORTABLE_GP_RACE_SESSION_ROSTER_ERROR; goto report; }
    if(previous!=NULL&&portable_gp_cup_session_resume_race(previous,session)!=
        PORTABLE_GP_CUP_SESSION_OK)goto report;
    session->retry_credits=retry_credits;
    if(cup!=NULL&&previous==NULL&&
       (portable_gp_race_session_handoff_launch_rng(session,103u)!=PORTABLE_GP_RACE_SESSION_OK||
        !portable_gp_item_art_clock_begin_direct(&session->item_art_clock,
            session->runtime.session.countdown_remaining,race.item_art_intro_rules)))goto report;
    while(ticks<60000u && (session->runtime.session.phase!=PORTABLE_RACE_PHASE_FINISHED ||
          (require_all && finished_count!=PORTABLE_GP_RACER_COUNT) ||
          (cup!=NULL && (session->roster.race_phase_7d4&15u)!=5u)))
    {
        /* The aid supplies ordinary buttons; the GP session owns all eight
         * drivers, countdown, collisions, course actors, recovery and laps. */
        const uint16_t buttons=portable_gp_race_smoke_control(session,ticks);
        PortableRaceTickDisposition disposition;
        status=portable_gp_race_session_tick(session,buttons,&disposition);
        star_ticks+=(session->roster.racers[0].lightning_status_6a!=0);
        mushroom_ticks+=(session->player.steering.driver_mode==8u);
        for(unsigned actor=0u;actor<PORTABLE_GP_GREEN_ACTOR_SLOTS;++actor){
            if(!portable_gp_green_actor(&session->roster,actor)->occupied)continue;
            if(portable_gp_green_actor_owner(&session->roster,actor)==0u)++green_ticks;else ++cpu_green_ticks;
        }
        for(unsigned actor=0u;actor<PORTABLE_GP_RED_ACTOR_SLOTS;++actor){
            const PortableGpRedShell *shell=portable_gp_red_actor(&session->roster,actor);
            if(!shell->occupied)continue;
            retained_red_ticks+=actor>=PORTABLE_GP_RACER_COUNT;
            if(portable_gp_red_actor_owner(&session->roster,actor)==0u)++red_ticks;
            else if(shell->kind_10==PORTABLE_GP_SPINY_SHELL_KIND)++cpu_spiny_ticks;
            else ++cpu_red_ticks;
        }
        for(unsigned actor=0u;actor<PORTABLE_GP_BANANA_ACTOR_SLOTS;++actor){
            const PortableGpBanana *banana=portable_gp_banana_actor(&session->roster,actor);
            if(!banana->occupied)continue;
            retained_banana_ticks+=actor>=PORTABLE_GP_RACER_COUNT;
            if(portable_gp_banana_actor_owner(&session->roster,actor)==0u){
                ++banana_ticks;banana_flight_ticks+=banana->phase==PORTABLE_GP_BANANA_THROWN;
            }else{
                ++cpu_banana_ticks;cpu_banana_flight_ticks+=banana->phase==PORTABLE_GP_BANANA_THROWN;
            }
        }
        ++ticks;
        finished_count=0u;
        for(unsigned i=0u;i<PORTABLE_GP_RACER_COUNT;++i)
            finished_count+=((session->roster.racers[i].source_flags & UINT32_C(0x20))!=0u &&
                session->roster.racers[i].route_wrap_91>=UINT8_C(0x80)+session->runtime.session.lap_count);
        if(status!=PORTABLE_GP_RACE_SESSION_OK) break;
    }
    ok=status==PORTABLE_GP_RACE_SESSION_OK &&
        session->runtime.session.phase==PORTABLE_RACE_PHASE_FINISHED &&
        session->runtime.session.laps_completed==session->runtime.session.lap_count &&
        (!require_all || finished_count==PORTABLE_GP_RACER_COUNT);
report:
    fprintf(ok?stdout:stderr,"GP smoke: %s course=%u character=%u status=%u ticks=%u phase=%u laps=%u/%u driver=%d finished=%u/8 star_ticks=%u mushroom_ticks=%u player_mushrooms=%u triple_greens=%u triple_reds=%u green_ticks=%u red_ticks=%u retained_red_ticks=%u green_hits=%u red_hits=%u banana_ticks=%u banana_flight_ticks=%u retained_banana_ticks=%u banana_hits=%u coins=%u boos=%u thefts=%u cpu_mushrooms=%u cpu_stars=%u cpu_reds=%u cpu_red_ticks=%u cpu_spinies=%u cpu_spiny_ticks=%u cpu_greens=%u cpu_green_ticks=%u cpu_bananas=%u cpu_banana_ticks=%u cpu_banana_flight_ticks=%u lightning_uses=%u player_lightning_hits=%u\n",
        ok?"PASS":"FAIL",course_id,character_id,(unsigned)status,ticks,
        session->runtime.session.phase,session->runtime.session.laps_completed,
        session->runtime.session.lap_count,session->runtime.last_driver_status,finished_count,star_ticks,mushroom_ticks,session->roster.player_mushroom_uses,session->roster.triple_green_uses,session->roster.triple_red_uses,green_ticks,red_ticks,retained_red_ticks,session->roster.green_shell_hits,session->roster.red_shell_hits,banana_ticks,banana_flight_ticks,retained_banana_ticks,session->roster.banana_hits,session->roster.coin_events,session->roster.boo_uses,session->roster.boo_thefts,session->roster.cpu_mushroom_uses,session->roster.cpu_star_uses,session->roster.cpu_red_uses,cpu_red_ticks,session->roster.cpu_spiny_uses,cpu_spiny_ticks,session->roster.cpu_green_uses,cpu_green_ticks,session->roster.cpu_banana_uses,cpu_banana_ticks,cpu_banana_flight_ticks,session->roster.lightning_events,session->roster.lightning_effect_events);
    fprintf(ok?stdout:stderr,"GP driving outcome: rank=%u rescues=%u race_ticks=%u\n",
        (unsigned)(session->roster.racers[0].rank_flags_ba>>1u),
        session->runtime.session.rescue_count,portable_race_session_total_ticks(&session->runtime.session));
    if (!ok)
        fprintf(stderr,"GP state: roster_stage=%u phase=%u script=%u callback=%u requests=%04x height=%d hop=%u rescues=%u target=%d,%d\n",
            session->failed_roster_stage,session->hazard_lakitu.phase,session->hazard_lakitu.script,
            session->hazard_lakitu.callback,session->player.hop.recovery_requests,
            session->player.pose.position_z,session->player.hop.mode,
            session->runtime.session.rescue_count,session->player.recovery_target_x_9e,
            session->player.recovery_target_y_a0);
    for(unsigned i=0u;i<8u;++i){
        const PortableGpRacer *r=&session->roster.racers[i];
        fprintf(ok?stdout:stderr,"racer=%u pos=%d,%d speed=%d flags=%08x checkpoint=%u wrap=%u mode=%u hop=%u response=%u recovery=%02x target=%d,%d resample=%u item=%04x roulette=%u\n",
            i,r->position_x_q16/65536,r->position_y_q16/65536,r->speed_q16,
            r->source_flags,r->route_index,r->route_wrap_91,r->mushroom_mode_84,
            r->lightning_state_78,r->hit_state_7e,r->recovery_material,
            r->recovery_target_x_9e,r->recovery_target_y_a0,r->recovery_checkpoint_sample,
            r->item_inventory.held_item_17a,r->item_inventory.active);
    }
    if(ok&&standings!=NULL){
        const PortableGpCupFinishRoute route=portable_gp_cup_session_finish_route(session);
        fprintf(stdout,"GP cup race: cup=%u class=%u track=%u rank=%u route=%u\n",
            cup->cup_index,engine_class,track,(unsigned)(session->roster.racers[0].rank_flags_ba>>1u),
            (unsigned)route);
        if(route==PORTABLE_GP_CUP_FINISH_RETRY_MENU){ok=2;goto done;}
        ok=route==PORTABLE_GP_CUP_FINISH_STANDINGS&&
           portable_gp_cup_session_begin_standings(session,previous,cup->cup_index,track,
               cup->source_headers[track],cup->track_set,standings)==PORTABLE_GP_CUP_SESSION_OK;
    }
done:
    free(presentation_storage);free(session);free(storage);
    return ok;
}

/* The existing cup aid confirms the shared failure scene after a real loss. */

#ifndef PORTABLE_ENABLE_SDL3
static int run_gp_failure_smoke(const PortableAssetPackView *pack,uint8_t credits)
{
    (void)pack;
    PortableGpFailureMenu menu;
    if(!portable_gp_failure_menu_init(&menu,credits))return 0;
    for(unsigned tick=0;tick<300u;++tick)
        if(!portable_gp_failure_menu_tick(&menu,tick==90u?8u:0u))return 0;
    return menu.phase==PORTABLE_GP_FAILURE_MENU_COMPLETE&&(unsigned)menu.action==(credits?1u:4u);
}

static int run_gp_cup_smoke(const PortableAssetPackView *pack,uint8_t cup_index,
    uint32_t character_id,uint8_t engine_class)
{
    PortableGpCupDefinition definition;
    PortableGpCupSession progress={0},standings={0};
    if(!portable_frontend_gp_cup(pack,cup_index,&definition))return 0;
    PortableGpScoringRules scoring;
    if(!portable_recovered_gp_scoring(pack,&scoring))return 0;
    uint8_t credits=scoring.initial_retry_credits;
    for(uint8_t track=0u;track<4u;++track){
        for(;;){
            const int result=run_gp_smoke(pack,definition.course_ids[track],character_id,1,
                engine_class,track!=0u?&progress:NULL,&definition,track,&standings,credits);
            if(result==0)return 0;
            if(result==1)break;
            if(!run_gp_failure_smoke(pack,credits))return 0;
            printf("GP cup failure: cup=%u class=%u track=%u credits=%u action=%s\n",
                cup_index,engine_class,track,credits,credits?"retry":"game-over");
            if(credits==0u)return 1;
            --credits;
            progress.retry_credits=credits;
        }
        if(track<3u&&portable_gp_cup_session_advance(&standings,&definition,&progress)!=
            PORTABLE_GP_CUP_SESSION_OK)return 0;
    }
    PortableGpCeremonySession ceremony;
    if(portable_gp_ceremony_session_init(&ceremony,&standings)!=PORTABLE_GP_CEREMONY_OK)
        return 0;
    const char *progress_path=getenv("MKSC_GP_PROGRESS");
    if(progress_path!=NULL&&progress_path[0]!='\0'&&
       portable_gp_progress_record_ceremony(progress_path,&ceremony)!=PORTABLE_GP_PROGRESS_OK)
        return 0;
    printf("GP cup rating: score=%d samples=%d,%d,%d,%d rating=%u coins=%u extra-unlocked=%u saved=%s\n",
        ceremony.driving_score,standings.driving_quality.samples[0],
        standings.driving_quality.samples[1],standings.driving_quality.samples[2],
        standings.driving_quality.samples[3],ceremony.performance_rating,
        ceremony.finishing_coin_total,ceremony.extra_cup_unlocked,
        ceremony.save_evaluation_pending?"no":"yes");
    printf("GP cup simulation: PASS cup=%u class=%u rank=%u points=%u; ceremony rendering requires SDL\n",
        cup_index,engine_class,ceremony.player_standing_rank,standings.cumulative_points[0]);

    return 1;
}

#endif

static int run_frontend_input_replay(const PortableAssetPackView *pack,
                                     const char *path)
{
    PortableFrontendDriverSlot drivers[PORTABLE_FRONTEND_DRIVER_CAPACITY];
    PortableFrontendCourseSlot courses[PORTABLE_FRONTEND_COURSE_CAPACITY];
    PortableFrontendConfig config;
    PortableFrontendFlow flow;
    PortableFrontendInputReplayStatus status;
    uint32_t tick_count=0u;
    size_t character_moves=0u;
    int cancelled=0;
    FILE *file;
    if(!build_frontend_config(pack,drivers,courses,&config)||
       !portable_frontend_flow_init(&flow,&config)||
       !portable_frontend_flow_settle_title(&flow)||
       !portable_frontend_configure_motion(pack,&flow)){
        fputs("faithful frontend replay configuration is unavailable\n",stderr);
        return 0;
    }
    const char *progress_path=getenv("MKSC_GP_PROGRESS");
    if(progress_path!=NULL&&progress_path[0]!='\0'){
        PortableGpProgress progress;
        const PortableGpProgressStatus loaded=portable_gp_progress_load(progress_path,&progress);
        if(loaded==PORTABLE_GP_PROGRESS_NOT_FOUND)portable_gp_progress_init(&progress);
        else if(loaded!=PORTABLE_GP_PROGRESS_OK)return 0;
        flow.title_variant=portable_gp_progress_title_variant(&progress);
        uint8_t access[3];
        for(uint8_t speed=0u;speed<3u;++speed)
            access[speed]=portable_gp_progress_cup_access(&progress,speed);
        if(!portable_frontend_flow_set_cup_access(&flow,access))return 0;
        for(uint8_t speed=0u;speed<3u;++speed)
            access[speed]=portable_gp_progress_extra_cup_access(&progress,speed);
        if(!portable_frontend_flow_set_extra_cup_access(&flow,access))return 0;
    }
    file=fopen(path,"rb");
    if(file==NULL){fprintf(stderr,"could not read frontend replay: %s\n",path);
        return 0;}
    status=portable_frontend_input_replay_read_file(file,&flow,&tick_count,&cancelled);
    fclose(file);
    if(status!=PORTABLE_FRONTEND_INPUT_REPLAY_OK){
        fprintf(stderr,"faithful frontend replay rejected: %s\n",
            portable_frontend_input_replay_status_name(status));
        return 0;
    }
    for(size_t index=0u;index<flow.event_count;++index)
        if(flow.events[index].page==PORTABLE_FRONTEND_PAGE_CHARACTER&&
           flow.events[index].type==PORTABLE_FRONTEND_EVENT_MOVE)
            ++character_moves;
    printf("faithful frontend replay: PASS ticks=%u page=%u checksum=%08x "
           "cancelled=%s character=%u character-moves=%zu mode=%u cup=%u class=%u course=%u\n",tick_count,
           (unsigned)flow.page,portable_frontend_flow_checksum(&flow),
           cancelled?"yes":"no",(unsigned)flow.character_cursor,
           character_moves,(unsigned)flow.mode_cursor,(unsigned)flow.course_cup,
           (unsigned)flow.race_speed,flow.selected_course);
    return 1;
}

int main(int argc, char **argv)
{
    portable_startup_begin();
#ifdef PORTABLE_ENABLE_SDL3
    portable_mobile_lifecycle_hint();
#endif
#if defined(_WIN32) && defined(PORTABLE_WINDOWS_GUI)
    /* Windowed builds have no console; reuse the caller's for CLI output
     * unless output is already redirected (pipes, files). */
    if (GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) == FILE_TYPE_UNKNOWN &&
        AttachConsole(ATTACH_PARENT_PROCESS)) {
        (void)freopen("CONOUT$", "w", stdout);
        (void)freopen("CONOUT$", "w", stderr);
    }
#endif
#ifdef __ANDROID__
    /* Android has no console: debug launches name a file for CLI output. */
    const char *stdio_log = getenv("MKSC_STDIO_LOG");
    if (stdio_log != NULL && stdio_log[0] != '\0' && freopen(stdio_log, "a", stdout) != NULL &&
        freopen(stdio_log, "a", stderr) != NULL) {
        setvbuf(stdout, NULL, _IOLBF, 0);
        setvbuf(stderr, NULL, _IOLBF, 0);
    }
#endif
    const int imported = portable_content_import_command(argc, argv);
    if (imported >= 0) return imported;
    const int rom_imported = portable_rom_import_command(argc, argv);
    if (rom_imported >= 0) return rom_imported;
    const int game_imported = portable_game_import_command(argc, argv);
    if (game_imported >= 0) return game_imported;
    /* No pack argument: use the imported pack, or run the first-start ROM
     * screen. --rom preselects the ROM; --first-run-smoke runs that screen
     * hidden, then the ordinary shell smoke route on the imported pack
     * (--first-run-tour: a menu tour instead). */
    static char first_run_pack[4096];
    static char *first_run_arguments[64];
    const int first_run_tour = argc >= 5 && strcmp(argv[1], "--first-run-tour") == 0;
    const int first_run_smoke = first_run_tour || (argc >= 4 && strcmp(argv[1], "--first-run-smoke") == 0);
    const int rom_launch = argc >= 3 && strcmp(argv[1], "--rom") == 0;
    if ((argc == 1 || rom_launch || first_run_smoke) && argc < 60) {
        const int found = portable_first_run(argc == 1 ? NULL : argv[2], first_run_smoke,
                                             first_run_pack, sizeof first_run_pack);
        if (found <= 0) return found < 0;
        int count = 0;
        first_run_arguments[count++] = argv[0];
        if (first_run_smoke) first_run_arguments[count++] = first_run_tour ? "--menu-tour" : "--shell-smoke";
        first_run_arguments[count++] = first_run_pack;
        for (int index = argc == 1 ? 1 : 3; index < argc; ++index) first_run_arguments[count++] = argv[index];
        first_run_arguments[count] = NULL;
        argv = first_run_arguments;
        argc = count;
    }
    char *launch_arguments[4] = {argv[0], "--time-trial", NULL, NULL};
    PortableSettingsOverrides settings_overrides={0};
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        printf("usage: %s [PACK] [--mods-dir PATH] [--mod ID ...] [--no-mods]\n"
               "       %s --rom ROM [launch options]   (first start with this ROM)\n"
               "       %s --import ROM [PACK]          (convert your ROM; default PACK is game.pack\n"
               "                                       in the data directory or MKSC_DATA_DIR)\n"
               "       %s --import-game ROM [MODS_DIR] (add Mario Kart DS / Mario Kart 64 (US) content as a mod;\n"
               "                                       default MODS_DIR is mods/ in the data directory)\n"
               "Without PACK, plays the imported game.pack, or asks for your ROM first;\n"
               "a pack from an older importer offers to convert the ROM again.\n"
               "Launch the title menu, then choose Mario GP or Time Trial.\n"
               "Keyboard: arrows move, Enter/Z choose, Space starts, Esc/X backs out.\n"
               "Controllers use the D-pad, A/B and Start.\n"
               "Launch options (temporary overrides after PACK):\n"
               "  --watch-mods/--no-watch-mods (reload changed mods in menus; F5 reloads manually)\n"
               "  --window-scale 1..8 --fullscreen/--windowed --vsync/--no-vsync\n"
               "  --aspect 3:2|16:9|21:9|WIDTH:HEIGHT|auto\n"
               "  --internal-scale 1..8 --fit-scaling/--integer-scaling\n"
               "  --filter nearest|linear --volume 0..100 --steering-deadzone 1..30000\n"
               "  --short-transitions/--original-transitions --interpolate/--no-interpolate\n"
               "  --render-fps 0..1000 (0 uncapped, interpolation must be enabled)\n"
               "  --wide-ui-backgrounds/--no-wide-ui-backgrounds (optional menu panoramas)\n"
               "  --touch-controls/--no-touch-controls (menu and Player 1 touch pad)\n"
               "  --artwork-lod original|high|max (available sprite artwork quality)\n"
               "  --race-renderer original|3d (optional 3D graphics with supported-content fallback)\n"
               "  --[no-]immersive-camera (closer chase on 3D courses; default off)\n"
               "  --[no-]smooth-3d --[no-]dynamic-camera --[no-]camera-shake (3D race motion)\n"
               "  --[no-]hd2d-dof --[no-]hd2d-bloom --[no-]hd2d-ao (GPU depth and glow)\n"
               "  --[no-]hd2d-grade --[no-]hd2d-fog --[no-]hd2d-light --[no-]hd2d-scale (GPU finish)\n"
               "  --[no-]drift-sparks --[no-]boost-effects --[no-]kart-trails (3D race particles)\n"
               "  --drift-extras off|tiers|tiers-air-boost (original 2D handling with optional boost rewards)\n"
               "    tiers: drift-release boosts; tiers-air-boost also adds air tricks and landing boosts.\n"
               "    Each selection keeps separate records and ghosts; modern physics belongs to 3D courses.\n"
               "  --driving-profile original|modern|modern-air (compatible aliases for drift extras)\n"
               "  --key-{accelerate,brake,left,right,hop,item,aim-up,aim-down} SCANCODE\n"
               "  --pad-{accelerate,brake,hop,item} BUTTON\n"
               "  --key2-{accelerate,brake,left,right,hop,item,aim-up,aim-down} SCANCODE (player 2)\n"
               "  --asset-source drivers|items|courses=ID|@base|auto (per-category priority)\n",
               argv[0], argv[0], argv[0], argv[0]);
        /* Split: C99 guarantees string literals only up to 4095 bytes. */
        printf("Development commands:\n"
               "  --recovered-smoke PACK [COURSE CHARACTER]\n"
               "  --time-trial-direct PACK COURSE CHARACTER\n"
               "  --gp-race PACK COURSE CHARACTER\n"
               "  --battle-menu-smoke PACK ARENA:0..3 CHARACTER [CLASS:0..2] [launch options]\n"
               "  --vs-smoke PACK PLAYERS:2..4 COURSE [SETTINGS] (local VS through the menu)\n"
               "  --battle-smoke PACK PLAYERS:2..4 ARENA:0..3 [SETTINGS] (local Battle through the menu)\n"
               "  --quick-run-menu-smoke PACK CUP CHARACTER [CLASS:0..2 [OPTIONS:0..7]] [SETTINGS]\n"
               "  --quick-run-replay-smoke PACK CUP CHARACTER [CLASS:0..2 [OPTIONS:0..7]] [SETTINGS]\n"
               "  --gp-smoke PACK COURSE CHARACTER [CLASS:0..2]\n"
               "  --gp-smoke-all PACK COURSE CHARACTER [CLASS:0..2]\n"
               "  --gp-cup-smoke PACK CUP:0..9|11..15 CHARACTER [CLASS:0..2 [RENDER_FPS:0|60|120|144]] [SETTINGS]\n"
               "  --shell-smoke PACK SAVE_DIRECTORY [CHARACTER] [SETTINGS] (SDL ordinary-menu Time Trial/results/GP route)\n"
               "  --menu-tour PACK SAVE_DIRECTORY SCRIPT [FRAME_DIRECTORY] [SETTINGS] (press ABSEUDLRlr/. on settled menus)\n"
               "  --time-trial-replay PACK INPUT COURSE CHARACTER [SETTINGS]\n"
               "  --time-trial-finished-replay PACK INPUT COURSE CHARACTER [SETTINGS]\n"
               "  --mesh-replay PACK INPUT [SETTINGS]\n"
               "  --mesh-finished-replay PACK INPUT [SETTINGS]\n"
               "  --frontend-input-replay PACK INPUT\n"
               "  --first-run-smoke ROM|-|keep SAVE_DIRECTORY [SETTINGS] (hidden first start or stale-pack\n"
               "                                       reimport, then --shell-smoke; - = remembered ROM)\n"
               "  --first-run-tour ROM|-|keep SAVE_DIRECTORY SCRIPT [...] (the same, then --menu-tour)\n"
               "  --export-course-pack-sections SKY COURSE TILES PALETTES ROM OUTPUT METADATA\n");
        return 0;
    }
    if (argc >= 2 && argv[1][0] != '-') {
        char detail[PORTABLE_SETTINGS_ERROR_CAPACITY];
        if(!portable_settings_cli(argc-2,argv+2,&settings_overrides,detail)){
            fprintf(stderr,"%s (see --help)\n",detail);return 2;
        }
        launch_arguments[2] = argv[1];
        argv = launch_arguments;
        argc = 3;
    }
    /* Headless race smokes take a trailing --mirror for mirror mode. */
    if(argc>3&&strcmp(argv[argc-1],"--mirror")==0&&(strcmp(argv[1],"--gp-smoke")==0||
       strcmp(argv[1],"--gp-smoke-all")==0||strcmp(argv[1],"--recovered-smoke")==0)){
        portable_mod_identity_set_mirror(1u);--argc;
    }
    const int menu = argc == 3 && strcmp(argv[1], "--time-trial") == 0;
    const int shell_smoke=argc>=4&&strcmp(argv[1],"--shell-smoke")==0;
    const int menu_tour=argc>=5&&strcmp(argv[1],"--menu-tour")==0;
    const int tour_frames=menu_tour&&argc>=6&&argv[5][0]!='-';
    const int trial = argc == 5 && strcmp(argv[1], "--time-trial-direct") == 0;
    const int gp = argc == 5 && strcmp(argv[1], "--gp-race") == 0;
    const int quick_replay=argc>=5&&strcmp(argv[1],"--quick-run-replay-smoke")==0;
    const int quick_smoke=quick_replay||(argc>=5&&strcmp(argv[1],"--quick-run-menu-smoke")==0);
    const int battle_smoke=argc>=5&&strcmp(argv[1],"--battle-menu-smoke")==0;
    const int gp_cup_smoke=argc>=5&&strcmp(argv[1],"--gp-cup-smoke")==0;
    const int local_battle=argc>=5&&strcmp(argv[1],"--battle-smoke")==0;
    const int local_smoke=local_battle||(argc>=5&&strcmp(argv[1],"--vs-smoke")==0);
    const int gp_smoke_all = (argc == 5 || argc == 6) && strcmp(argv[1], "--gp-smoke-all") == 0;
    const int gp_smoke = gp_smoke_all || ((argc == 5 || argc == 6) && strcmp(argv[1], "--gp-smoke") == 0);
    const int smoke = (argc == 3 || argc == 5) &&
        strcmp(argv[1], "--recovered-smoke") == 0;
    const int replay = argc >= 6 &&
        (strcmp(argv[1], "--time-trial-replay") == 0 ||
         strcmp(argv[1], "--time-trial-finished-replay") == 0);
    const int mesh_replay = argc >= 4 &&
        (strcmp(argv[1], "--mesh-replay") == 0 ||
         strcmp(argv[1], "--mesh-finished-replay") == 0);
    const int frontend = argc == 4 &&
        strcmp(argv[1], "--frontend-input-replay") == 0;
    uint32_t course_id = 0u, character_id = 0u, engine_class = 0u, render_fps=0u, quick_options=0u;
    if (!menu && !shell_smoke && !menu_tour && !trial && !gp && !gp_smoke && !gp_cup_smoke && !quick_smoke && !battle_smoke && !local_smoke && !smoke && !replay && !mesh_replay && !frontend) {
        fprintf(stderr, "usage: %s PACK (see --help for development commands)\n", argv[0]);
        return 2;
    }
    if ((trial || gp || gp_smoke || (smoke && argc == 5)) &&
        (!parse_id(argv[3], &course_id) || !parse_id(argv[4], &character_id)))
        return 2;
    if((gp_cup_smoke||quick_smoke||battle_smoke)&&((strcmp(argv[3],"0")!=0&&!parse_id(argv[3],&course_id))||
        (battle_smoke?course_id>=4u:(course_id>=PORTABLE_FRONTEND_COURSE_CUPS||
            course_id==PORTABLE_FRONTEND_ARENA_CUP))||!parse_id(argv[4],&character_id)))return 2;
    uint32_t local_players=0u;
    if(local_smoke){
        char detail[PORTABLE_SETTINGS_ERROR_CAPACITY];
        if(!parse_id(argv[3],&local_players)||local_players<2u||local_players>4u||
           ((strcmp(argv[4],"0")!=0&&!parse_id(argv[4],&course_id))||(local_battle&&course_id>=4u)))return 2;
        if(!portable_settings_cli(argc-5,argv+5,&settings_overrides,detail)){
            fprintf(stderr,"%s (see --help)\n",detail);return 2;
        }
    }
    if(menu_tour){
        char detail[PORTABLE_SETTINGS_ERROR_CAPACITY];
        if(!portable_settings_cli(argc-5-tour_frames,argv+5+tour_frames,&settings_overrides,detail)){
            fprintf(stderr,"%s (see --help)\n",detail);return 2;
        }
    }
    const int shell_character=shell_smoke&&argc>=5&&argv[4][0]!='-';
    if(shell_smoke){
        char detail[PORTABLE_SETTINGS_ERROR_CAPACITY];
        character_id=1u;
        if(shell_character&&!parse_id(argv[4],&character_id))return 2;
        if(!portable_settings_cli(argc-4-shell_character,argv+4+shell_character,
                                  &settings_overrides,detail)){
            fprintf(stderr,"%s (see --help)\n",detail);return 2;
        }
    }
    const int battle_class=battle_smoke && argc>=6 && argv[5][0]!='-';
    const int quick_class=quick_smoke && argc>=6 && argv[5][0]!='-';
    const int quick_option=quick_class && argc>=7 && argv[6][0]!='-';
    const int cup_class=gp_cup_smoke && argc>=6 && argv[5][0]!='-';
    const int cup_render=cup_class && argc>=7 && argv[6][0]!='-';
    if(battle_smoke||quick_smoke||gp_cup_smoke){
        char detail[PORTABLE_SETTINGS_ERROR_CAPACITY];
        const int start=quick_smoke?(quick_option?7:quick_class?6:5):gp_cup_smoke?(cup_render?7:cup_class?6:5):(battle_class?6:5);
        if(!portable_settings_cli(argc-start,argv+start,&settings_overrides,detail)){
            fprintf(stderr,"%s (see --help)\n",detail);return 2;
        }
    }
    if (((gp_smoke && argc>=6) || battle_class || quick_class || cup_class) &&
        ((strcmp(argv[5],"0")!=0 && !parse_id(argv[5],&engine_class)) ||
         engine_class>2u)) return 2;
    if(quick_option&&
        ((strcmp(argv[6],"0")!=0&&!parse_id(argv[6],&quick_options))||quick_options>7u))return 2;
    if(cup_render&&
        ((strcmp(argv[6],"0")!=0&&!parse_id(argv[6],&render_fps))||
         (render_fps!=0u&&render_fps!=60u&&render_fps!=120u&&render_fps!=144u)))return 2;
    if(mesh_replay){
        char detail[PORTABLE_SETTINGS_ERROR_CAPACITY];
        if(!portable_settings_cli(argc-4,argv+4,&settings_overrides,detail)){
            fprintf(stderr,"%s (see --help)\n",detail);return 2;
        }
    }
    if(replay){
        char detail[PORTABLE_SETTINGS_ERROR_CAPACITY];
        if(!portable_settings_cli(argc-6,argv+6,&settings_overrides,detail)){
            fprintf(stderr,"%s (see --help)\n",detail);return 2;
        }
    }
    if (replay && (!parse_id(argv[4], &course_id) ||
                   !parse_id(argv[5], &character_id)))
        return 2;
    size_t size = 0u;
    uint8_t *bytes = NULL;
    PortableAssetPackView pack;
#ifdef PORTABLE_ENABLE_SDL3
    const int shell_launch = menu || menu_tour || shell_smoke || quick_smoke ||
                             battle_smoke || gp_cup_smoke || local_smoke;
    if (shell_launch) {
        const int loaded = portable_startup_pack_take(argv[2], !menu, &bytes, &size, &pack);
        if (loaded != 1) { portable_loading_close(); return loaded < 0 ? 0 : 1; }
    } else
#endif
    {
        const uint64_t pack_started = portable_startup_clock();
        bytes = read_file(argv[2], &size);
        if (!bytes) { fprintf(stderr, "could not read pack: %s\n", argv[2]); return 1; }
        const PortableAssetPackStatus status = portable_asset_pack_open(bytes, size, &pack);
        if (status != PORTABLE_ASSET_PACK_OK) {
            fprintf(stderr, "pack rejected: %s\n", portable_asset_pack_status_name(status));
            free(bytes); return 1;
        }
        portable_startup_phase("pack read/validate", pack_started);
    }
    int result = 1;
    if(menu_tour){
#ifdef PORTABLE_ENABLE_SDL3
        result=run_menu_tour_smoke(&pack,argv[3],argv[4],tour_frames?argv[5]:NULL,&settings_overrides);
#else
        fputs("menu tour requires the SDL build\n",stderr);
#endif
    } else if(shell_smoke){
#ifdef PORTABLE_ENABLE_SDL3
        result=run_game_shell_smoke(&pack,argv[3],character_id,&settings_overrides);
#else
        fputs("shell smoke requires the SDL build\n",stderr);
#endif
    } else if (frontend) {
        result = !run_frontend_input_replay(&pack, argv[3]);
    } else if (mesh_replay) {
        result = portable_mesh_time_trial_verify_cli(&pack, argv[3], &settings_overrides,
            strcmp(argv[1], "--mesh-finished-replay") == 0);
    } else if (replay) {
        PortableSettings replay_settings;portable_settings_defaults(&replay_settings);
        portable_settings_apply(&replay_settings,&settings_overrides);
        PortableRecoveredReplayHeader header={0};
        FILE *input=fopen(argv[3],"r");
        const int header_ok=input!=NULL && portable_recovered_replay_inspect_header_file(input,&header);
        if(input!=NULL)fclose(input);
        if(!header_ok){fputs("invalid Time Trial replay header\n",stderr);free(bytes);return 1;}
        if((settings_overrides.mask & (UINT64_C(1)<<PORTABLE_SETTING_DRIVING_PROFILE))==0u)
            replay_settings.driving_profile=(uint8_t)(header.version==PORTABLE_RECOVERED_REPLAY_AIR_VERSION?2u:
                header.version==PORTABLE_RECOVERED_REPLAY_MODERN_VERSION?1u:0u);
        PortableDrivingProfile *profile=NULL;
        const PortableAssetPackView *replay_base=&pack;
        if(replay_settings.driving_profile){
            profile=portable_driving_profile_create(&pack,replay_settings.driving_profile);
            if(profile==NULL){fputs("could not prepare Modern replay rules\n",stderr);free(bytes);return 1;}
            replay_base=portable_driving_profile_pack(profile);
        }
        portable_mod_identity_set_mirror(replay_settings.mirror);
        PortableModSet *mods=NULL;
        if(replay_settings.mod_count!=0u && !settings_overrides.no_mods){
            if(settings_overrides.mods_dir==NULL){
                fputs("replay mods require --mods-dir\n",stderr);portable_driving_profile_free(profile);free(bytes);return 2;
            }
            mods=portable_mods_load(replay_base,settings_overrides.mods_dir,
                (const char (*)[PORTABLE_MOD_ID_CAPACITY])replay_settings.mods,replay_settings.mod_count);
            if(mods==NULL){fputs("could not compose replay mods\n",stderr);portable_driving_profile_free(profile);free(bytes);return 1;}
        }
        char source_error[PORTABLE_MOD_ERROR_CAPACITY];
        if(mods&&!portable_mods_select_assets(mods,replay_base,&pack,
            (const char (*)[PORTABLE_MOD_ID_CAPACITY])replay_settings.asset_sources,source_error,sizeof source_error)){
            fprintf(stderr,"%s\n",source_error);portable_mods_free(mods);
            portable_driving_profile_free(profile);free(bytes);return 1;
        }
        portable_mod_identity_set_current(portable_mods_identity(mods));
        const PortableAssetPackView *replay_pack=mods?portable_mods_pack(mods):replay_base;
        result = !portable_driving_profile_publish(replay_pack) || !run_time_trial_replay(replay_pack, argv[3], course_id, character_id,
            strcmp(argv[1], "--time-trial-finished-replay") == 0);
        portable_mod_identity_set_current(NULL);portable_mods_free(mods);
        portable_mod_identity_set_driving_profile(NULL);portable_driving_profile_free(profile);
    } else if(local_smoke){
#ifdef PORTABLE_ENABLE_SDL3
        result=run_local_multiplayer_smoke(&pack,(uint8_t)local_battle,(uint8_t)local_players,course_id,&settings_overrides);
#else
        fputs("local multiplayer aid requires the SDL build\n",stderr);
#endif
    } else if(battle_smoke){
#ifdef PORTABLE_ENABLE_SDL3
        result=run_battle_menu_smoke(&pack,(uint8_t)course_id,character_id,(uint8_t)engine_class,&settings_overrides);
#else
        fputs("Battle menu aid requires the SDL build\n",stderr);
#endif
    } else if(quick_smoke){
#ifdef PORTABLE_ENABLE_SDL3
        result=run_quick_run_menu_smoke(&pack,(uint8_t)course_id,character_id,(uint8_t)engine_class,(uint8_t)quick_options,(uint8_t)quick_replay,&settings_overrides);
#else
        fputs("Quick Run menu aid requires the SDL build\n",stderr);
#endif
    } else if(gp_cup_smoke){
#ifdef PORTABLE_ENABLE_SDL3
        result=run_gp_cup_menu_smoke(&pack,(uint8_t)course_id,character_id,(uint8_t)engine_class,cup_render,(uint16_t)render_fps,&settings_overrides);
#else
        if(cup_render||settings_overrides.mask!=0u||settings_overrides.mods_set||settings_overrides.mods_dir!=NULL||settings_overrides.no_mods){fputs("render cadence and settings require the SDL host\n",stderr);free(bytes);return 2;}
        result=!run_gp_cup_smoke(&pack,(uint8_t)course_id,character_id,(uint8_t)engine_class);
#endif
    } else if (gp_smoke) {
        PortableGpScoringRules scoring;
        result = !portable_recovered_gp_scoring(&pack,&scoring) || !run_gp_smoke(&pack,course_id,character_id,gp_smoke_all,(uint8_t)engine_class,NULL,NULL,0u,NULL,scoring.initial_retry_credits);
    } else if (menu || trial || gp) {
#ifdef PORTABLE_ENABLE_SDL3
        if (menu) result = run_time_trial_sdl_settings(&pack,&settings_overrides);
        else if (trial) result = run_direct_time_trial_sdl(&pack, course_id, character_id);
        else result = run_gp_race_sdl(&pack, course_id, character_id);
#else
        fputs("Interactive play requires the SDL build (check.py debug --sdl).\n", stderr);
#endif
    } else {
        PortableContentSelection selection;
        PortableContentStatus content = portable_content_registry_select(
            &pack, course_id, character_id, &selection);
        PortableRecoveredPackStorage *storage = malloc(sizeof *storage);
        PortableRecoveredRaceView race;
        PortableRecoveredSmokeReport report = {0};
        if (storage != NULL && content == PORTABLE_CONTENT_OK) {
            PortableRecoveredPackStatus opened = portable_course_open_race(
                &pack, &selection, storage, &race, portable_mod_identity_mirror());
            if (opened == PORTABLE_RECOVERED_PACK_OK) {
                PortableRecoveredSmokeStatus raced = portable_recovered_race_smoke_run(
                    &race, &race.initial_driver, &report);
                result = raced != PORTABLE_RECOVERED_SMOKE_OK || report.laps_completed != race.rules.lap_count;
                fprintf(result ? stderr : stdout,
                    "recovered pack smoke: %s status=%s laps=%u ticks=%u race_ticks=%u\n",
                    result ? "FAIL" : "PASS", portable_recovered_smoke_status_name(raced),
                    report.laps_completed, report.scheduled_ticks, report.race_ticks);
            } else {
                fprintf(stderr, "race payload rejected: %s\n",
                    portable_recovered_pack_status_name(opened));
            }
        } else {
            fprintf(stderr, "race selection failed course=%u character=%u: %s\n",
                course_id, character_id, storage == NULL ? "out of memory" :
                portable_content_status_name(content));
        }
        free(storage);
    }
    free(bytes);
#ifdef PORTABLE_ENABLE_SDL3
    portable_loading_close();
#endif
    return result;
}
