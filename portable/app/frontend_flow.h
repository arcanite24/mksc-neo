#ifndef PORTABLE_FRONTEND_FLOW_H
#define PORTABLE_FRONTEND_FLOW_H

#include <stddef.h>
#include <stdint.h>
#include "../core/menu_heading.h"

#include "pause_menu.h"
#include "quick_run_options.h"
#include "track_set.h"

#define PORTABLE_FRONTEND_DRIVER_SLOTS 9u /* One 3x3 page; slot 4 is Random. */
#define PORTABLE_FRONTEND_DRIVER_PAGES 8u /* Stock page plus seven roster pages. */
#define PORTABLE_FRONTEND_DRIVER_CAPACITY \
    (PORTABLE_FRONTEND_DRIVER_SLOTS * PORTABLE_FRONTEND_DRIVER_PAGES)
#define PORTABLE_FRONTEND_RANDOM_SLOT 4u
#define PORTABLE_FRONTEND_CUPS 5u
#define PORTABLE_FRONTEND_ARENA_CUP 10u
#define PORTABLE_FRONTEND_CUSTOM_CUP 11u
#define PORTABLE_FRONTEND_COURSE_CUPS PORTABLE_TRACK_SET_COURSE_CUPS
#define PORTABLE_FRONTEND_TRACKS_PER_CUP 4u
#define PORTABLE_FRONTEND_EVENT_CAPACITY 256u
#define PORTABLE_FRONTEND_SHORT_TRANSITION_TICKS 16u
#define PORTABLE_FRONTEND_TITLE_MODE_TICKS 132u /* title exit18 + player/mode entry114 */
#define PORTABLE_FRONTEND_MODE_TRIAL_TICKS 61u /* travel33 + setup1/fade2/rows25 */
#define PORTABLE_FRONTEND_GP_CHARACTER_TICKS 84u /* medallion exit66 + grid entry18 */
#define PORTABLE_FRONTEND_TRIAL_CHARACTER_TICKS 84u
#define PORTABLE_FRONTEND_CHARACTER_COURSE_TICKS 117u /* character exit99 + course entry18 */
#define PORTABLE_FRONTEND_COURSE_RACE_TICKS 73u /* AF94 states 10..13: hold 40, fade 32 */
#define PORTABLE_FRONTEND_COURSE_RACE_FADE_TICKS 32u
#define PORTABLE_FRONTEND_REPEAT_DELAY 24u
#define PORTABLE_FRONTEND_REPEAT_PERIOD 6u
#define PORTABLE_FRONTEND_MAX_PLAYERS 4u

typedef enum
{
    PORTABLE_FRONTEND_PAGE_TITLE_INTRO = 1,
    PORTABLE_FRONTEND_PAGE_TITLE_MENU,
    PORTABLE_FRONTEND_PAGE_MODE,
    PORTABLE_FRONTEND_PAGE_TIME_TRIAL,
    PORTABLE_FRONTEND_PAGE_CHARACTER,
    PORTABLE_FRONTEND_PAGE_COURSE,
    PORTABLE_FRONTEND_PAGE_RACE,
    PORTABLE_FRONTEND_PAGE_PAUSE,
    PORTABLE_FRONTEND_PAGE_RESULTS,
    PORTABLE_FRONTEND_PAGE_RECORDS
} PortableFrontendPage;

typedef enum
{
    PORTABLE_FRONTEND_BUTTON_NONE = 0,
    PORTABLE_FRONTEND_BUTTON_A = 1u << 0,
    PORTABLE_FRONTEND_BUTTON_B = 1u << 1,
    PORTABLE_FRONTEND_BUTTON_SELECT = 1u << 2,
    PORTABLE_FRONTEND_BUTTON_START = 1u << 3,
    PORTABLE_FRONTEND_BUTTON_RIGHT = 1u << 4,
    PORTABLE_FRONTEND_BUTTON_LEFT = 1u << 5,
    PORTABLE_FRONTEND_BUTTON_UP = 1u << 6,
    PORTABLE_FRONTEND_BUTTON_DOWN = 1u << 7,
    PORTABLE_FRONTEND_BUTTON_L = 1u << 8,
    PORTABLE_FRONTEND_BUTTON_R = 1u << 9
} PortableFrontendButton;

typedef enum
{
    PORTABLE_FRONTEND_EVENT_PAGE_ENTER = 1,
    PORTABLE_FRONTEND_EVENT_MOVE,
    PORTABLE_FRONTEND_EVENT_ACCEPT,
    PORTABLE_FRONTEND_EVENT_OPEN,
    PORTABLE_FRONTEND_EVENT_BACK,
    PORTABLE_FRONTEND_EVENT_UNAVAILABLE,
    PORTABLE_FRONTEND_EVENT_RACE_LAUNCH,
    PORTABLE_FRONTEND_EVENT_RACE_FINISH,
    PORTABLE_FRONTEND_EVENT_RACE_RESTART,
    PORTABLE_FRONTEND_EVENT_PAUSE,
    PORTABLE_FRONTEND_EVENT_RESUME,
    PORTABLE_FRONTEND_EVENT_CHARACTER_LOCK,
    PORTABLE_FRONTEND_EVENT_RACE_CANCEL,
    PORTABLE_FRONTEND_EVENT_RECORD_ERASE,
    PORTABLE_FRONTEND_EVENT_GHOST_ERASE,
    PORTABLE_FRONTEND_EVENT_GHOST_EXPORT,
    PORTABLE_FRONTEND_EVENT_GHOST_RECEIVE,
    PORTABLE_FRONTEND_EVENT_RECEIVED_GHOST_ERASE,
    PORTABLE_FRONTEND_EVENT_HORN, /* value: stock grid slot of the driver's seat */
    PORTABLE_FRONTEND_EVENT_TRACK_SET, /* value: new course_cup*4 (B89C song 147) */
    PORTABLE_FRONTEND_EVENT_KART_TRICK /* value: 0 R hop (156,159), 1 L spin (123) */
} PortableFrontendEventType;

/* Entry page*9+slot. Page 0 is the stock grid; later pages hold roster
 * drivers. Zero voice_id/seat_slot/art_slot mean the stock defaults: the
 * driver's own ID and its own slot. */
typedef struct
{
    uint32_t content_id;
    uint8_t available;
    uint32_t voice_id; /* Stock driver whose menu voices, horn and rev play. */
    uint8_t seat_slot; /* Stock slot of that driver: fallback art and horn. */
    uint8_t art_slot; /* Grid slot the driver's own portrait art is placed for. */
    uint8_t mesh_only; /* A 3D-domain driver: 3D courses only, art from the 3D pack. */
    uint8_t platform;  /* 0 the base game; else the content platform it races on. */
} PortableFrontendDriverSlot;

/* A kart of the 3D domain; owner_default marks the kart its owner races
 * when the player picks none. */
typedef struct
{
    uint32_t content_id, owner;
    uint8_t owner_default;
} PortableFrontendKartSlot;
#define PORTABLE_FRONTEND_KART_CAPACITY 64u

typedef struct
{
    uint32_t content_id;
    uint8_t cup;
    uint8_t track;
    uint8_t available;
} PortableFrontendCourseSlot;

/* A content platform is a mod that provides both 3D courses and its own
 * drivers (an imported game). Its drivers fill their own character pages;
 * locking one of them limits the course screen to that platform's group and
 * other players to its pages. Platform 0 is the base game: its pages race the
 * 2D sets and the 3D groups that belong to no platform.
 *
 * A cup of 3D-domain courses, listed after the 2D track sets under its
 * group (one per providing mod, e.g. an imported game). Rows follow the
 * catalog's cup order; modes are META bits (1 GP, 2 TT, 4 Quick, 8 VS,
 * 16 Battle). gp marks a complete four-course GP cup. Names are borrowed. */
#define PORTABLE_FRONTEND_MESH_CUP_COURSES 8u
#define PORTABLE_FRONTEND_MESH_CUP_CAPACITY 96u
#define PORTABLE_FRONTEND_MESH_GROUP_CAPACITY 16u
typedef struct
{
    const char *name;
    uint32_t cup_id;
    uint32_t course_ids[PORTABLE_FRONTEND_MESH_CUP_COURSES];
    uint32_t course_modes[PORTABLE_FRONTEND_MESH_CUP_COURSES];
    const char *course_names[PORTABLE_FRONTEND_MESH_CUP_COURSES];
    uint8_t group, course_count, gp;
    uint8_t explore_only; /* Row bits: no physics profile, so Confirm explores. */
} PortableFrontendMeshCup;

typedef struct
{
    const PortableFrontendDriverSlot *drivers;
    size_t driver_count;
    const PortableFrontendCourseSlot *courses;
    size_t course_count;
} PortableFrontendConfig;

enum
{
    PORTABLE_FRONTEND_MESH_RACE = 1,
    PORTABLE_FRONTEND_MESH_EXPLORE = 2
};

typedef struct
{
    uint32_t tick;
    uint32_t value;
    uint8_t type;
    uint8_t page;
} PortableFrontendEvent;

typedef enum
{
    PORTABLE_FRONTEND_REPLAY_NONE = 0,
    PORTABLE_FRONTEND_REPLAY_FINISH
} PortableFrontendReplayCommand;

typedef struct
{
    uint16_t buttons;
    uint8_t command;
    uint32_t value;
} PortableFrontendReplayFrame;

/* title_8000350 owns the retargetable list position separately from selection. */
typedef struct {
    int32_t sine_q15[8];
    int16_t y,start_y;
    uint8_t phase,ticks,configured; /* 1 snapshots the start; 2 interpolates. */
} PortableFrontendTitleCursor;

typedef struct
{
    PortableFrontendConfig config;
    PortableFrontendPage page;
    PortableFrontendPage pending_page;
    uint32_t tick;
    uint32_t page_ticks;
    uint16_t buttons;
    uint16_t repeated;
    uint16_t triggered;
    uint16_t hold_ticks;
    uint16_t transition_ticks;
    uint16_t transition_total_ticks;
    uint16_t transition_out_ticks;
    uint8_t transition_from_mode_phase;
    /* Render-only phase supplied by frontend_presentation, never a second clock. */
    uint8_t motion_stage; /* 0 settled, 1 entry, 2 exit. */
    uint16_t motion_tick;
    uint16_t motion_delay;
    uint8_t motion_player_entry;
    uint8_t title_cursor;
    PortableFrontendTitleCursor title_cursor_motion;
    PortableMenuHeadingRules heading_rules;
    PortableMenuHeading heading;
    PortableMenuMedallion medallion;
    int32_t heading_sine[1024];
    uint8_t heading_configured,heading_static_mask;
    /* Local multiplayer: title row 1 picks 2..max_players (platform-supplied,
     * zero means 2), then VS or Battle and one locked driver per player. */
    uint8_t players, max_players, player_count_open;
    uint8_t character_player;
    uint32_t player_characters[PORTABLE_FRONTEND_MAX_PLAYERS];
    uint8_t short_transitions; /* Presentation option; original timing by default. */
    uint32_t title_ship_ticks, title_ambient_rng;
    uint16_t title_star_counter, title_star_wait;
    uint8_t title_star_state; /* Night title: waiting, setup, falling. */
    uint8_t title_variant; /* 0 normal, 1 sunset, 2 night (from saved GP progress). */
    uint8_t mode_cursor;
    uint8_t mode_phase;
    uint8_t race_speed;
    PortableQuickRunOptions quick_run_options;
    PortableQuickRunOptions vs_options; /* Local VS keeps its own 58B0 values. */
    uint8_t quick_options_open, quick_options_cursor; /* 58B0 window: Time Trial, Quick Run or VS. */
    uint8_t ghost_display_off; /* Time Trial window bits: 0 own ghost, 1 received ghost. */
    uint8_t time_trial_cursor;
    uint8_t character_cursor; /* Slot 0..8 on character_page. */
    uint8_t character_page;
    uint8_t character_locked;
    /* Single player, when the platform enables kart_choice: a locked driver
     * turns through karts with Left/Right before confirming. */
    PortableFrontendKartSlot karts[PORTABLE_FRONTEND_KART_CAPACITY];
    uint8_t kart_count, kart_choice, kart_cursor;
    uint32_t selected_kart; /* 0 until a driver locks with karts listed */
    uint8_t cup_access[3]; /* Zero means unrestricted for content-only callers. */
    uint8_t extra_cup_access[3]; /* Zero means locked; cups 5..9 are Extra. */
    uint8_t course_cup; /* GBA 0..4, Extra 5..9, arenas 10, custom 11..15. */
    /* 3D cups: 0 shows the 2D sets above; otherwise the 1-based group, its
     * cup (config index) and course_track as the row. A confirmed row is
     * requested from the platform, which clears the request when it acts. */
    const PortableFrontendMeshCup *mesh_cups; /* Borrowed; see set_mesh_cups. */
    size_t mesh_cup_count;
    const char *const *mesh_group_names;
    const uint8_t *mesh_group_platforms; /* Borrowed; NULL means every group is platform 0. */
    uint8_t mesh_group_count;
    uint8_t mesh_group, mesh_cup;
    uint8_t platform; /* Of player one's locked driver; 0 until a platform driver locks. */
    uint8_t mesh_request;
    uint32_t mesh_request_course;
    uint8_t course_track;
    uint8_t record_erase_choice; /* Confirmation defaults to No. */
    uint8_t records_top; /* First of the records list's eight visible rows. */
    uint8_t ghost_choices; /* Bits: local, received1, received2; supplied by platform. */
    uint8_t ghost_choice; /* Index 0..2, retained through launch. */
    uint8_t record_ghost_slot; /* 0 local, 1..2 received, as source DC28. */
    uint8_t course_phase; /* Records: 2 board, 3 erase course, 4 ghost, 5 erase ghost, 6 receive. */
    uint8_t cup_motion_phase,cup_motion_tick; /* lock/unlock,17 source turns. */
    uint8_t gp_preview_track,gp_preview_ticks; /* visual only; GP starts track0. */
    uint8_t paused;
    PortablePauseMenu pause_menu;
    uint32_t selected_character;
    uint32_t selected_course;
    uint32_t result_ticks;
    uint32_t character_selection_tick, character_lock_tick;
    uint8_t character_animation_timing; /* Bit0 selection, bit1 lock timestamps. */
    uint32_t character_trick_until; /* 60AC/6850 kart busy until this tick. */
    PortableFrontendEvent events[PORTABLE_FRONTEND_EVENT_CAPACITY];
    size_t event_count;
    uint8_t event_overflow;
} PortableFrontendFlow;

/* Pages are driver_count/9; moving past a side column changes page. */
uint8_t portable_frontend_flow_driver_pages(const PortableFrontendFlow *flow);
const PortableFrontendDriverSlot *portable_frontend_flow_driver_at(
    const PortableFrontendFlow *flow,uint8_t page,uint8_t slot);
const PortableFrontendDriverSlot *portable_frontend_flow_find_driver(
    const PortableFrontendFlow *flow,uint32_t content_id);
/* Stock driver ID whose menu voice/rev a driver uses (itself when unlisted). */
uint32_t portable_frontend_flow_voice_id(const PortableFrontendFlow *flow,
                                         uint32_t content_id);
int portable_frontend_flow_set_ghost_choices(PortableFrontendFlow *flow,uint8_t mask);
/* Lists the karts a locked driver may turn through; 0 when count is too large. */
int portable_frontend_flow_set_karts(PortableFrontendFlow *flow,const PortableFrontendKartSlot *karts,
                                     size_t count);

/* GP uses its selected class; Time Trial reads the 150cc unlock column.
 * Custom cups are always unlocked once their catalog rows exist (GP needs
 * all four tracks; other modes need one). */
uint8_t portable_frontend_flow_cup_access(const PortableFrontendFlow *flow);
/* C680 records list for the current track set: available courses of open
 * cups in cup order, as absolute cup*4+track. Returns the count. */
enum { PORTABLE_FRONTEND_RECORDS_MAX=20, PORTABLE_FRONTEND_RECORDS_ROWS=8 };
unsigned portable_frontend_flow_records_list(const PortableFrontendFlow *flow,
    uint8_t entries[PORTABLE_FRONTEND_RECORDS_MAX]);
/* Lists 3D cups after the 2D sets (borrowed until replaced; NULL/0 clears).
 * Each cup names a group below group_count and 1..8 nonzero courses;
 * group_platforms (NULL: all 0) names each group's platform. */
int portable_frontend_flow_set_mesh_cups(PortableFrontendFlow *flow,
    const PortableFrontendMeshCup *cups, size_t count, const char *const *group_names,
    const uint8_t *group_platforms, uint8_t group_count);
/* Platform of a character page (its drivers share one). */
uint8_t portable_frontend_flow_page_platform(const PortableFrontendFlow *flow, uint8_t page);
/* Whether the locked platform may race a 2D set (group 0) or 3D group (1-based). */
int portable_frontend_flow_group_allowed(const PortableFrontendFlow *flow, uint8_t group);
/* The 1-based 3D group of a platform; 0 when it has none listed. */
uint8_t portable_frontend_flow_platform_group(const PortableFrontendFlow *flow, uint8_t platform);
/* META mode bit the selected mode races (1 GP, 2 TT, 4 Quick, 8 VS, 16 Battle). */
uint32_t portable_frontend_flow_mesh_mode(const PortableFrontendFlow *flow);
/* Whether a 3D cup / group offers anything in the selected mode. */
int portable_frontend_flow_mesh_cup_open(const PortableFrontendFlow *flow, size_t cup);
int portable_frontend_flow_mesh_group_open(const PortableFrontendFlow *flow, uint8_t group);
int portable_frontend_flow_set_extra_cup_access(PortableFrontendFlow *flow,const uint8_t masks[3]);
int portable_frontend_flow_set_cup_access(PortableFrontendFlow *flow,const uint8_t masks[3]);
int portable_frontend_flow_init(PortableFrontendFlow *flow,
                                const PortableFrontendConfig *config);
int portable_frontend_flow_settle_title(PortableFrontendFlow *flow);
/* Call only after every event consumer has handled the pending batch. */
void portable_frontend_flow_consume_events(PortableFrontendFlow *flow);
int portable_frontend_flow_tick(PortableFrontendFlow *flow, uint16_t buttons);
int portable_frontend_flow_finish_race(PortableFrontendFlow *flow,
                                       uint32_t result_ticks);
int portable_frontend_flow_cancel_race(PortableFrontendFlow *flow);
int portable_frontend_flow_cancel_race_to(PortableFrontendFlow *flow,
                                          PortableFrontendPage destination);
int portable_frontend_flow_replay(PortableFrontendFlow *flow,
                                  const PortableFrontendReplayFrame *frames,
                                  size_t frame_count);
/* Copy FEM1 sine indices32,64,...256. NULL restores old-pack settled drawing. */
int portable_frontend_flow_set_title_cursor_sine(PortableFrontendFlow *flow,
    const int32_t samples[8]);
uint32_t portable_frontend_flow_checksum(const PortableFrontendFlow *flow);

/* Fixed-tick source heading owner. NULL rules disable it for legacy packs. */
int portable_frontend_flow_set_heading_rules(PortableFrontendFlow *flow,
    const PortableMenuHeadingRules *rules,const int32_t sine[1024]);

#endif
