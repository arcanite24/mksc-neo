#ifndef PORTABLE_MODERN_MENU_H
#define PORTABLE_MODERN_MENU_H

#include "native_image.h"
#include "frontend_flow.h"
#include "gp_progress_store.h"
#include "settings.h"
#include "mod_store.h"
#include "time_trial_records.h"
#include "ui_canvas.h"
#include "menu_driver_model.h"

/* The modern (MK8-styled) presentation of the shared frontend flow. It reads
 * the same flow state as the original menus and draws at window resolution;
 * input, navigation and timing stay with the flow and the platform shell. */
typedef struct ModernMenu ModernMenu;
typedef struct PortableModManager PortableModManager;

/* Borrowed pending selection; rendering never validates or applies it. */
typedef struct
{
    const PortableStore *store;
    unsigned store_open,store_cursor,store_details,store_action,store_busy;
    const PortableModManager *manager;
    unsigned cursor, show_order, reviewing; /* 0 list, 1 apply review, 3 unsaved. */
    unsigned advanced; /* Advanced rows (load order, Reload, folders) shown. */
    unsigned detail_page, action_open, action_cursor;
    uint8_t notice_error;
    const char *notice;
} ModernMenuMods;

/* Word-aware full-resolution pages; Classic keeps its native-sized slices. */
unsigned modern_menu_mod_detail_pages(const PortableModManager *manager, unsigned cursor,
                                     int show_order, int advanced, const char *notice);

typedef enum
{
    MODERN_MENU_OPTIONS_CLOSED = 0,
    MODERN_MENU_OPTIONS_MAIN = 1,
    MODERN_MENU_OPTIONS_BINDINGS = 2,
    MODERN_MENU_OPTIONS_ENHANCEMENTS = 3,
    MODERN_MENU_OPTIONS_MODS = 4,
    MODERN_MENU_OPTIONS_GAMES = 5,
    MODERN_MENU_OPTIONS_PAUSE_GRAPHICS = 6 /* In-race pause page: no category tabs. */
} ModernMenuOptionsPage;

#define MODERN_MENU_GAMES_MAX 4u
typedef struct {
    const PortableModManager *manager;
    UiImage art[MODERN_MENU_GAMES_MAX]; /* Per library game; pixels NULL until loaded. */
    unsigned game,row;
    const char *notice;
    uint8_t notice_error;
    uint8_t art_loading[MODERN_MENU_GAMES_MAX]; /* That game's art is still loading on a worker. */
} ModernMenuGames;

/* One row of a native options page, already formatted by the shell. */
typedef struct
{
    const char *label;
    char value[32];
    const char *help; /* Optional one-line description. */
    uint8_t adjustable; /* Left/right change the value. */
} ModernMenuOptionRow;

enum { MODERN_MENU_OPTION_ROWS = 24 };

typedef struct
{
    ModernMenuOptionsPage page;
    const char *title;
    ModernMenuOptionRow rows[MODERN_MENU_OPTION_ROWS];
    unsigned row_count, cursor;
    const char *notice; /* Save result or a page-specific status line. */
    const char *hint;   /* Footer instructions for the page. */
    uint8_t capturing;  /* Binding capture: waiting for a key or button. */
    const ModernMenuMods *mods;
    const ModernMenuGames *games;
} ModernMenuOptions;

typedef struct
{
    const PortableAssetPackView *pack;
    const PortableAssetPackView *pack_3d; /* Optional: art of 3D-only drivers. */
    const PortableFrontendFlow *flow;
    const PortableGpProgress *progress; /* Optional: medals on the cup page. */
    const PortableTimeTrialRecords *records; /* Optional: records/ghost boards. */
    const ModernMenuOptions *options; /* Optional overlay. */
    uint8_t gamepad; /* Show controller glyphs rather than keyboard keys. */
    uint8_t driving_profile; /* 0 Original; nonzero names the modern profile badge. */
    /* Optional META preview picture of a 3D cup course (ARGB view). */
    int (*mesh_preview)(const void *owner, uint32_t course_id, PortableNativeImage *out);
    const void *mesh_preview_owner;
} ModernMenuView;

/* First-start screens shown before any game data exists: ROM import, the
 * import in progress, an outdated-data prompt and a short setup page. */
typedef enum
{
    MODERN_SETUP_IMPORT,
    MODERN_SETUP_IMPORTING,
    MODERN_SETUP_REIMPORT,
    MODERN_SETUP_PREFERENCES
} ModernMenuSetupScreen;

typedef struct
{
    ModernMenuSetupScreen screen;
    uint32_t tick;             /* Animation clock at the menu tick rate. */
    const char *status;        /* Optional result or error line. */
    uint8_t status_error;
    const char *detail;        /* Reimport: what changed and which ROM is offered. */
    const char *rom_path;      /* Reimport: the remembered ROM, if any. */
    uint8_t gamepad;
    /* Preferences: rows plus a final "start" row at index row_count. */
    ModernMenuOptionRow rows[6];
    unsigned row_count, cursor;
} ModernMenuSetup;

/* In-race pause: the frozen race behind a dimmed list of choices. */
enum { MODERN_MENU_PAUSE_ITEMS = 5 };

typedef enum
{
    MODERN_PAUSE_CONTINUE,
    MODERN_PAUSE_RETRY,
    MODERN_PAUSE_CHANGE_COURSE,
    MODERN_PAUSE_CHANGE_DRIVER,
    MODERN_PAUSE_QUIT
} ModernMenuPauseKind;

typedef struct
{
    const uint32_t *frame; /* The race picture (ARGB, opaque, rows packed) and where it sits. */
    int frame_width, frame_height;
    float frame_x, frame_y, frame_w, frame_h;
    const char *subtitle; /* Optional: course or cup. */
    const char *labels[MODERN_MENU_PAUSE_ITEMS];
    ModernMenuPauseKind kinds[MODERN_MENU_PAUSE_ITEMS];
    unsigned item_count, cursor;
    uint32_t tick; /* Ticks since the pause opened. */
    uint8_t gamepad;
    uint8_t external_background; /* Caller composites the race; frame may be NULL. */
    const ModernMenuOptions *graphics; /* Optional: replaces the list with the graphics rows. */
} ModernMenuPause;

enum { MODERN_MENU_RESULT_ROWS = 8, MODERN_MENU_RESULT_ACTIONS = 5 };

/* Hosts resolve ordering, times, points and available actions. The renderer
 * borrows these values for one frame; it never advances the results owner. */
typedef struct
{
    uint32_t driver_id;
    uint32_t portrait_id; /* Optional frontend image, with its complete authored bounds. */
    const char *name;     /* Optional: otherwise use the driver name or content ID. */
    char value[32];       /* Formatted time, points, balloons or other primary result. */
    char detail[48];      /* Optional secondary result or record status. */
    unsigned rank;       /* Displayed rank; zero leaves this column empty. */
    uint8_t highlighted;
} ModernMenuResultRow;

typedef struct
{
    const PortableAssetPackView *pack;
    const uint32_t *frame; /* Opaque ARGB race picture, packed rows. */
    int frame_width, frame_height;
    float frame_x, frame_y, frame_w, frame_h;
    const char *title;
    const char *subtitle;
    ModernMenuResultRow rows[MODERN_MENU_RESULT_ROWS];
    uint32_t course_id; /* Optional content identity for a results handoff card. */
    unsigned row_count;
    const char *labels[MODERN_MENU_RESULT_ACTIONS];
    unsigned item_count, cursor;
    uint8_t disabled_mask; /* Bit i dims action i; navigation stays with the host. */
    const char *notice;
    const char *hint; /* Optional footer instructions instead of the generic action prompts. */
    uint32_t tick; /* Presentation clock, supplied by the results host. */
    uint8_t gamepad;
    uint8_t external_background; /* Premultiplied UI only over caller-owned race texture. */
    uint8_t ceremony; /* Preserve a staged podium picture; row/action panels supply contrast. */
    uint8_t live_frame; /* Host: recopy the race picture each present; the race keeps running. */
} ModernMenuResults;

typedef enum
{
    MODERN_TRANSITION_LOADING,
    MODERN_TRANSITION_ENTRY,
    MODERN_TRANSITION_RESULTS,
    MODERN_TRANSITION_CUP
} ModernMenuTransitionKind;

/* One immutable handoff picture. The host supplies progress from an existing
 * clock; drawing never waits, advances a race, or changes input availability. */
typedef struct
{
    const PortableAssetPackView *pack;
    const uint32_t *frame; /* Optional opaque ARGB race picture, packed rows. */
    int frame_width, frame_height;
    float frame_x, frame_y, frame_w, frame_h;
    const char *title;
    const char *subtitle;
    uint32_t course_id; /* Zero means unknown; catalogs resolve preview/name art. */
    uint32_t elapsed, duration; /* Positive duration; elapsed clamps. Loading persists,
                               * other kinds reveal the base picture at completion. */
    ModernMenuTransitionKind kind;
    uint8_t gamepad;
    uint8_t external_background; /* UI only over a caller-retained texture. */
} ModernMenuTransition;

/* Content or startup loading: what is loading and which reported step is
 * running. Progress is the step count; nothing is estimated. */
typedef struct
{
    const char *title;
    const char *detail; /* Optional: the mods or data being prepared. */
    const char *const *phases;
    unsigned phase_count, phase;
    int cancelling; /* 0 running, 1 cancelling, 2 closing after the step. */
    uint8_t cancellable; /* 0 hides the cancel hint: the step cannot stop. */
    uint32_t tick;  /* Animation clock at the menu tick rate. */
    uint8_t gamepad;
} ModernMenuLoading;

ModernMenu *modern_menu_create(void);
void modern_menu_destroy(ModernMenu *menu);
/* Retire decoded artwork before freeing/replacing its content owner. */
void modern_menu_content_changed(ModernMenu *menu);
/* The 3D driver preview owner, shared with the classic character page; it
 * is forgotten with the decoded artwork. NULL only on allocation failure. */
PortableMenuDriverModel *modern_menu_driver_model(ModernMenu *menu);
/* Renders one frame. Returns 0 only for invalid arguments or flow states the
 * modern menus do not present (the caller reports them). */
int modern_menu_render(ModernMenu *menu, const ModernMenuView *view, UiCanvas *canvas);
int modern_menu_render_setup(ModernMenu *menu, const ModernMenuSetup *setup, UiCanvas *canvas);
int modern_menu_render_pause(ModernMenu *menu, const ModernMenuPause *pause, UiCanvas *canvas);
int modern_menu_render_results(ModernMenu *menu, const ModernMenuResults *results, UiCanvas *canvas);
int modern_menu_render_transition(ModernMenu *menu, const ModernMenuTransition *transition, UiCanvas *canvas);

/* live 0 draws the still layer (backdrop, logo, card, footer) for a size;
 * live 1 redraws only the card contents inside modern_menu_loading_live_rect
 * over a copy of that layer. */
int modern_menu_render_loading(ModernMenu *menu, const ModernMenuLoading *loading, UiCanvas *canvas, int live);
void modern_menu_loading_live_rect(int width, int height, int rect[4]);

/* Embedded project artwork; available before any game pack or fonts. */
const UiImage *modern_menu_logo_image(void);

#endif
