#include "graphics_options.h"

#include "frontend_image_renderer.h"
#include "menu_chrome_renderer.h"
#include "title_renderer.h"
#include "menu_font.h"
#include "ui_text.h"

#include <stdio.h>
#include <string.h>

_Static_assert(PORTABLE_PAUSE_GRAPHICS_ROWS <= MODERN_MENU_OPTION_ROWS, "Options capacity");

int portable_option_step(PortableSetting field, int old_value, int delta)
{
    const int step = field == PORTABLE_SETTING_VOLUME ? 5 : field == PORTABLE_SETTING_DEADZONE ? 1000 : 1;
    int value = old_value + delta * step;
    if (field != PORTABLE_SETTING_INTERNAL_SCALE && field != PORTABLE_SETTING_WINDOW_SCALE &&
        field != PORTABLE_SETTING_VOLUME && field != PORTABLE_SETTING_DEADZONE)
        value = !old_value;
    if (field == PORTABLE_SETTING_VOLUME)
        value = value < 0 ? 0 : value > 100 ? 100 : value;
    if (field == PORTABLE_SETTING_DEADZONE)
        value = value < 1 ? 1 : value > 30000 ? 30000 : value;
    if (field == PORTABLE_SETTING_RENDER_FPS)
    {
        static const int rates[] = {0, 60, 120, 144};
        int index = 0;
        for (int i = 0; i < 4; ++i)
            if (rates[i] == old_value)
                index = i;
        value = rates[(index + delta + 4) % 4];
    }
    if (field == PORTABLE_SETTING_ASPECT_WIDTH)
    {
        static const int aspects[] = {240, 284, 373, 0};
        int index = 0;
        for (int i = 0; i < 4; ++i)
            if (aspects[i] == old_value)
                index = i;
        value = aspects[(index + delta + 4) % 4];
    }
    return value;
}

void portable_option_value_text(const PortableSettings *settings, PortableSetting field, char value[24])
{
    const uint32_t number = portable_setting_get(settings, field);
    if (field == PORTABLE_SETTING_ASPECT_WIDTH)
    {
        if (number == 240u)
            snprintf(value, 24u, "3:2");
        else if (number == 284u)
            snprintf(value, 24u, "16:9");
        else if (number == 373u)
            snprintf(value, 24u, "21:9");
        else if (number == 0u)
            snprintf(value, 24u, "AUTO");
        else
            snprintf(value, 24u, "%.2f:1", (double)number / 160.0);
    }
    else if (field == PORTABLE_SETTING_FIT_SCALING)
        snprintf(value, 24u, "%s", number ? "FIT" : "INTEGER");
    else if (field == PORTABLE_SETTING_RENDER_FPS)
        snprintf(value, 24u, number ? "%u" : "UNCAPPED", number);
    else if (field == PORTABLE_SETTING_FILTER)
        snprintf(value, 24u, "%s", number ? "LINEAR" : "NEAREST");
    else if (field == PORTABLE_SETTING_INTERNAL_SCALE || field == PORTABLE_SETTING_WINDOW_SCALE ||
             field == PORTABLE_SETTING_VOLUME || field == PORTABLE_SETTING_DEADZONE)
        snprintf(value, 24u, "%u", number);
    else
        snprintf(value, 24u, "%s", number ? "ON" : "OFF");
}

void portable_option_title_case(char *text)
{
    int start = 1;
    for (; *text != '\0'; ++text)
    {
        const char c = *text;
        if (c >= 'A' && c <= 'Z' && !start)
            *text = (char)(c - 'A' + 'a');
        start = c == ' ' || c == '-' || c == '/' || (c >= '0' && c <= '9');
    }
}

enum { OPTION_ROW_Y = 26 };

void portable_option_page(const PortableAssetPackView *pack, PortableSoftwareFrame *frame, const char *title,
                          unsigned first, unsigned shown, unsigned total)
{
    (void)portable_menu_panel(pack, frame, 0, 0, 240u, 160u);
    portable_menu_text(pack, frame, 16, 8, title);
    if (first > 0u)
        (void)portable_frontend_image_draw(pack, PORTABLE_TITLE_UP, frame, 204, 22, 0, 0, 240, 160);
    if (first + shown < total)
        (void)portable_frontend_image_draw(pack, PORTABLE_TITLE_DOWN, frame, 220, 22, 0, 0, 240, 160);
}

void portable_option_row(const PortableAssetPackView *pack, PortableSoftwareFrame *frame, unsigned line,
                         int selected, const char *label, const char *value)
{
    const int y = OPTION_ROW_Y + (int)line * PORTABLE_OPTION_ROW_STEP;
    if (selected)
        (void)portable_frontend_image_draw(pack, PORTABLE_MENU_OPTION_CURSOR, frame, 14, y + 8, 0, 0, 240, 160);
    portable_menu_text(pack, frame, 20, y, label);
    const char *shown = portable_ui_text_resolve(frame->ui_text, value);
    portable_menu_text(pack, frame, 226 - (int)strlen(shown) * PORTABLE_MENU_FONT_WIDTH, y, shown);
}

typedef struct
{
    const char *label, *help;
    PortableSetting field;
    uint8_t is_setting;
} GraphicsRow;

/* Display first, then rendering; the last two are the Enhancements rows that shape the picture. */
static const GraphicsRow graphics_rows[PORTABLE_PAUSE_GRAPHICS_ROWS] = {
    {"Fullscreen", "Fill the whole display.", PORTABLE_SETTING_FULLSCREEN, 1},
    {"Window Scale", "Window size as a multiple of the original 240x160 screen.", PORTABLE_SETTING_WINDOW_SCALE, 1},
    {"VSync", "Sync presentation to the display to avoid tearing.", PORTABLE_SETTING_VSYNC, 1},
    {"Filter", "Nearest keeps pixels crisp; Linear smooths them in races.", PORTABLE_SETTING_FILTER, 1},
    {"Display Scaling", "Integer keeps square pixels; Fit uses the whole window.", PORTABLE_SETTING_FIT_SCALING, 1},
    {"Aspect Ratio", "Widescreen races show more of the track.", PORTABLE_SETTING_ASPECT_WIDTH, 1},
    {"Internal Scale", "Race rendering resolution as a multiple of the original.", PORTABLE_SETTING_INTERNAL_SCALE, 1},
    {"Interpolation", "Smooth motion between game ticks at high refresh rates.", PORTABLE_SETTING_INTERPOLATE, 1},
    {"Render FPS", "Frame rate cap while racing. Uncapped follows the display.", PORTABLE_SETTING_RENDER_FPS, 1},
    {"Race Renderer", "3D draws the track as geometry where supported; takes effect at the next race.",
     PORTABLE_SETTING_RACE_RENDERER, 1},
    {"HD-2D Graphics", "Extra Juicy enables all seven GPU effects on 3D courses.", PORTABLE_SETTING_HD2D_DOF, 0},
};

const char *portable_graphics_label(unsigned row)
{
    return row < PORTABLE_PAUSE_GRAPHICS_ROWS ? graphics_rows[row].label : "";
}

const char *portable_graphics_help(unsigned row)
{
    return row < PORTABLE_PAUSE_GRAPHICS_ROWS ? graphics_rows[row].help : "";
}

int portable_graphics_row_field(unsigned row, PortableSetting *field)
{
    if (row >= PORTABLE_PAUSE_GRAPHICS_ROWS || !graphics_rows[row].is_setting)
        return 0;
    *field = graphics_rows[row].field;
    return 1;
}

void portable_graphics_fill(PortablePauseGraphics *graphics, const PortableSettings *settings)
{
    static const char *const hd_presets[] = {"OFF", "EXTRA JUICY", "CUSTOM"};
    for (unsigned row = 0u; row < PORTABLE_PAUSE_GRAPHICS_ROWS; ++row)
    {
        PortableSetting field;
        char text[24];
        if (!portable_graphics_row_field(row, &field))
            snprintf(text, sizeof text, "%s", hd_presets[portable_settings_hd2d_preset(settings)]);
        else if (field == PORTABLE_SETTING_RACE_RENDERER)
            snprintf(text, sizeof text, "%s", settings->race_renderer ? "3D" : "ORIGINAL");
        else
            portable_option_value_text(settings, field, text);
        snprintf(graphics->values[row], sizeof graphics->values[row], "%s", text);
    }
}

void portable_graphics_render_classic(const PortableAssetPackView *pack, PortableSoftwareFrame *frame,
                                      const PortablePauseGraphics *graphics)
{
    const unsigned cursor = graphics->cursor < PORTABLE_PAUSE_GRAPHICS_ROWS ? graphics->cursor : 0u;
    const unsigned first = cursor >= PORTABLE_OPTION_PAGE_ROWS ? cursor - PORTABLE_OPTION_PAGE_ROWS + 1u : 0u;
    portable_option_page(pack, frame, "GRAPHICS", first, PORTABLE_OPTION_PAGE_ROWS, PORTABLE_PAUSE_GRAPHICS_ROWS);
    for (unsigned line = 0u; line < PORTABLE_OPTION_PAGE_ROWS; ++line)
    {
        const unsigned row = first + line;
        char label[32];
        snprintf(label, sizeof label, "%s", graphics_rows[row].label);
        for (char *c = label; *c != '\0'; ++c)
            if (*c >= 'a' && *c <= 'z')
                *c = (char)(*c - 'a' + 'A');
        portable_option_row(pack, frame, line, row == cursor, label, graphics->values[row]);
    }
    portable_menu_text_wrapped(pack, frame, 12, PORTABLE_OPTION_FOOTER_Y, 27u, PORTABLE_OPTION_ROW_STEP, 2u,
                               graphics->notice != NULL ? graphics->notice : "LEFT/RIGHT CHANGE - B BACK");
}

void portable_graphics_modern_options(ModernMenuOptions *options, const PortablePauseGraphics *graphics)
{
    *options = (ModernMenuOptions){.page = MODERN_MENU_OPTIONS_PAUSE_GRAPHICS, .title = "Graphics",
                                   .cursor = graphics->cursor, .notice = graphics->notice};
    for (unsigned row = 0u; row < PORTABLE_PAUSE_GRAPHICS_ROWS; ++row)
    {
        ModernMenuOptionRow *out = &options->rows[options->row_count++];
        out->label = graphics_rows[row].label;
        out->help = graphics_rows[row].help;
        out->adjustable = 1u;
        char value[PORTABLE_PAUSE_GRAPHICS_VALUE];
        snprintf(value, sizeof value, "%s", graphics->values[row]);
        portable_option_title_case(value);
        snprintf(out->value, sizeof out->value, "%s", value);
    }
}

const PortableSettings *portable_graphics_pause_update(PortablePauseGraphics *graphics,
                                                       const PortableGraphicsHook *hook)
{
    const int delta = graphics->edit;
    graphics->edit = 0;
    if (!graphics->open)
        return NULL;
    if (hook == NULL || hook->current == NULL || hook->edit == NULL)
    {
        graphics->open = 0u;
        return NULL;
    }
    const PortableSettings *changed = NULL;
    if (delta != 0)
    {
        const char *notice = NULL;
        changed = hook->edit(hook->user, graphics->cursor, delta, &notice);
        graphics->notice = notice;
    }
    portable_graphics_fill(graphics, hook->current(hook->user));
    return changed;
}
