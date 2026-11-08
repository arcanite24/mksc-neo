#ifndef PORTABLE_GRAPHICS_OPTIONS_H
#define PORTABLE_GRAPHICS_OPTIONS_H
#include "modern_menu.h"
#include "pause_menu.h"
#include "settings.h"
#include "software_frame.h"
#include "frontend_presentation.h"

/* Option rows shared by the Options screen and the in-race pause graphics page:
 * value stepping, value text and the classic (58B0 window) row drawing. */

/* The value one Left/Right press moves `field` to (delta -1 or +1); the caller
 * validates it with portable_setting_set. */
int portable_option_step(PortableSetting field, int old_value, int delta);
/* The value in the original menus' capitals ("UNCAPPED", "16:9"). */
void portable_option_value_text(const PortableSettings *settings, PortableSetting field, char value[24]);
/* Capitals to title case for the modern menus. */
void portable_option_title_case(char *text);

enum
{
    PORTABLE_OPTION_PAGE_ROWS = 6u,
    PORTABLE_OPTION_FOOTER_Y = 114,
    PORTABLE_OPTION_ROW_STEP = 14
};
void portable_option_page(const PortableAssetPackView *pack, PortableSoftwareFrame *frame, const char *title,
                          unsigned first, unsigned shown, unsigned total);
void portable_option_row(const PortableAssetPackView *pack, PortableSoftwareFrame *frame, unsigned line,
                         int selected, const char *label, const char *value);

/* Pause graphics rows, in page order. Rows that map to one setting report it. */
const char *portable_graphics_label(unsigned row);
const char *portable_graphics_help(unsigned row);
/* 1 and the setting when the row is one plain setting; 0 for HD-2D Graphics. */
int portable_graphics_row_field(unsigned row, PortableSetting *field);
/* Formats every row's value from `settings` into the pause page. */
void portable_graphics_fill(PortablePauseGraphics *graphics, const PortableSettings *settings);
/* Classic: the window page over a native 240x160 view. */
void portable_graphics_render_classic(const PortableAssetPackView *pack, PortableSoftwareFrame *frame,
                                      const PortablePauseGraphics *graphics);
/* Modern: the same rows as an options page for the pause renderer. */
void portable_graphics_modern_options(ModernMenuOptions *options, const PortablePauseGraphics *graphics);

/* How a race host applies an edit it cannot make itself: the shell steps the
 * row, applies it to the window, renderer and audio, saves it like the Options
 * screen and returns the settings now in effect (NULL when refused). */
typedef struct
{
    void *user;
    const PortableSettings *(*current)(void *user);
    const PortableSettings *(*edit)(void *user, unsigned row, int delta, const char **notice);
} PortableGraphicsHook;

/* Once per paused tick: runs a requested edit through `hook`, then refreshes
 * the page's values. Returns the settings in effect when an edit changed a
 * value (the host re-reads its copies), else NULL. Without a hook the page
 * closes. */
const PortableSettings *portable_graphics_pause_update(PortablePauseGraphics *graphics,
                                                       const PortableGraphicsHook *hook);
#endif
