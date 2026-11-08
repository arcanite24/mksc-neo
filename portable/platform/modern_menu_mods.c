#include "modern_menu_internal.h"
#include "mod_menu_renderer.h"

#include <stdio.h>
#include <string.h>

enum { MODS_DETAIL_PAGE_BYTES = 128u };

static size_t mods_detail_line_end(const char *text, size_t start, size_t limit)
{
    const UiTextStyle style = ui_text_style(UI_FONT_BOLD, 28.0f, MM_WHITE);
    char line[MODS_DETAIL_PAGE_BYTES + 1u];
    size_t last_space = start;
    for (size_t end = start; end < limit; ++end)
    {
        if (text[end] == '\n')
            return end + 1u;
        line[end - start] = text[end];
        line[end - start + 1u] = '\0';
        if (ui_text_width(&style, line) > 650.0f && end > start)
            return last_space > start ? last_space : end;
        if (text[end] == ' ' || text[end] == '\t')
            last_space = end + 1u;
    }
    return limit;
}

static size_t mods_detail_end(const char *text, size_t length, size_t start)
{
    size_t limit = length - start <= MODS_DETAIL_PAGE_BYTES
        ? length : start + MODS_DETAIL_PAGE_BYTES;
    /* Keep every byte, including separators, in exactly one page. A single
     * overlong token is split at the bound instead of preventing progress. */
    if (limit < length)
        for (size_t end = limit; end > start; --end)
            if (text[end - 1u] == ' ' || text[end - 1u] == '\n' || text[end - 1u] == '\t')
            {
                limit = end;
                break;
            }
    size_t at = start;
    for (unsigned line = 0u; line < 6u && at < limit; ++line)
        at = mods_detail_line_end(text, at, limit);
    return at;
}

static unsigned mods_detail_pages(const char *text, size_t *starts)
{
    const size_t length = strlen(text);
    size_t at = 0u;
    unsigned pages = 0u;
    do
    {
        if (starts != NULL)
            starts[pages] = at;
        ++pages;
        at = mods_detail_end(text, length, at);
    } while (at < length);
    if (starts != NULL)
        starts[pages] = length;
    return pages;
}

unsigned modern_menu_mod_detail_pages(const PortableModManager *manager, unsigned cursor,
                                     int show_order, int advanced, const char *notice)
{
    char text[PORTABLE_MOD_DETAIL_CAPACITY];
    (void)portable_mod_menu_detail(manager, cursor, show_order, advanced, notice, text);
    return mods_detail_pages(text, NULL);
}

/* Pending models are borrowed for this frame. The shell owns discovery,
 * validation, installation and publication; these panels only explain them. */
static const PortableModCatalogEntry *mods_entry(const PortableModCatalog *catalog,
                                                const char *id)
{
    if (catalog != NULL && id != NULL)
        for (size_t i = 0u; i < catalog->count && i < PORTABLE_MOD_CATALOG_LIMIT; ++i)
            if (strcmp(catalog->entries[i].id, id) == 0)
                return &catalog->entries[i];
    return NULL;
}

static void mods_text(MmCtx *x, const char *text, float px, float py,
                      float width, float size, uint32_t color)
{
    UiTextStyle style = ui_text_style(UI_FONT_BOLD, mm_s(x, size), color);
    style = ui_text_fit(&style, text, mm_s(x, width));
    ui_text_draw_centered(x->c, &style, mm_x(x, px), mm_y(x, py), UI_ALIGN_LEFT, text, 1.0f);
}

static void mods_list(MmCtx *x, const ModernMenuMods *mods,
                       const PortableModCatalog *catalog, size_t count, size_t selected)
{
    const unsigned visible = 6u;
    const size_t first = selected >= visible ? selected - visible + 1u : 0u;
    mods_text(x, mods->reviewing ? "Review pending load order" : mods->show_order ? "Load order (advanced)" : "Your mods",
              176.0f, 294.0f, 760.0f, 34.0f, MM_WHITE);
    for (size_t row = 0u; row < visible && first + row < count; ++row)
    {
        const size_t index = first + row;
        const char *id = mods->show_order ? portable_mod_manager_id(mods->manager, index)
            : catalog != NULL && index < catalog->count ? catalog->entries[index].id : NULL;
        const PortableModCatalogEntry *entry = mods_entry(catalog, id);
        const PortableModMenuRow kind = mods->show_order ? PORTABLE_MOD_ROW_MOD
            : portable_mod_menu_row(mods->manager, index, mods->advanced != 0u);
        const char *label = entry != NULL && entry->valid && entry->manifest.name[0] != '\0'
            ? entry->manifest.name : id != NULL ? id
            : portable_mod_menu_row_label(kind, mods->advanced != 0u, 0);
        const int rank = portable_mod_manager_index(mods->manager, id);
        char state[32] = "";
        if (mods->show_order)
            snprintf(state, sizeof state, "%u", (unsigned)index + 1u);
        else if (id == NULL)
            snprintf(state, sizeof state, "%s", kind >= PORTABLE_MOD_ROW_ORDER ? "Advanced" : "");
        else if (entry == NULL || !entry->valid)
            snprintf(state, sizeof state, "Invalid");
        else if (rank >= 0)
            snprintf(state, sizeof state, "On");
        else
            snprintf(state, sizeof state, "Off");
        const float y = 338.0f + (float)row * 84.0f;
        mm_tab(x, kind >= PORTABLE_MOD_ROW_ORDER ? 196.0f : 164.0f, y,
               kind >= PORTABLE_MOD_ROW_ORDER ? 780.0f : 812.0f, 76.0f, label,
               state[0] != '\0' ? state : NULL, index == selected, 1.0f,
               id != NULL || mods->show_order ? MM_ICON_LIST : kind == PORTABLE_MOD_ROW_ADVANCED ? MM_ICON_GEAR : MM_ICON_PLAY,
               0u, id != NULL && !mods->show_order && rank < 0);
    }
    if (count == 0u)
        mods_text(x, "No mods enabled", 176.0f, 390.0f, 760.0f, 36.0f, MM_WHITE);
    char position[80];
    snprintf(position, sizeof position, "%u of %u  /  %u on",
             count ? (unsigned)selected + 1u : 0u, (unsigned)count,
             (unsigned)portable_mod_manager_count(mods->manager));
    mods_text(x, position, 176.0f, 886.0f, 760.0f, 28.0f, MM_WHITE);
}

static void mods_details(MmCtx *x, const ModernMenuMods *mods)
{
    char text[PORTABLE_MOD_DETAIL_CAPACITY];
    (void)portable_mod_menu_detail(mods->manager, mods->cursor, mods->show_order != 0u,
        mods->advanced != 0u, mods->notice, text);
    /* Even one-byte pages fit this bound; no allocation or model mutation. */
    size_t starts[PORTABLE_MOD_DETAIL_CAPACITY + 1u];
    const unsigned pages = mods_detail_pages(text, starts);
    const unsigned page = mods->detail_page % pages;
    char heading[64];
    snprintf(heading, sizeof heading, "%s / Details %u of %u",
             mods->notice_error ? "Action failed" : mods->reviewing ? "Apply review" : "Selected",
             page + 1u, pages);
    mods_text(x, heading, 1080.0f, 294.0f, 650.0f, 32.0f,
              mods->notice_error ? UINT32_C(0xffff9c85) : MM_YELLOW);
    const size_t offset = starts[page];
    char detail[MODS_DETAIL_PAGE_BYTES + 1u];
    const size_t bytes = starts[page + 1u] - offset;
    memcpy(detail, text + offset, bytes);
    detail[bytes] = '\0';
    UiTextStyle style = ui_text_style(UI_FONT_BOLD, mm_s(x, 28.0f), MM_WHITE);
    size_t at = 0u;
    for (unsigned row = 0u; at < bytes && row < 6u; ++row)
    {
        const size_t end = mods_detail_line_end(detail, at, bytes);
        char line[MODS_DETAIL_PAGE_BYTES + 1u];
        memcpy(line, detail + at, end - at);
        line[end - at] = '\0';
        if (end > at && line[end - at - 1u] == '\n')
            line[end - at - 1u] = '\0';
        ui_text_draw(x->c, &style, mm_x(x, 1080.0f), mm_y(x, 340.0f + (float)row * 34.0f),
                     UI_ALIGN_LEFT, line, 1.0f);
        at = end;
    }
    if (mods->action_open && !mods->reviewing)
    {
        static const char *const actions[5] = {
            "Move earlier", "Move later", "Disable", "Previous details", "Next details"};
        for (unsigned i = 0u; i < 5u; ++i)
            mm_tab(x, 1064.0f, 564.0f + (float)i * 68.0f, 692.0f, 60.0f,
                   actions[i], NULL, i == mods->action_cursor, 1.0f, MM_ICON_NONE, 0u, 0);
    }
    else
    {
        const char *message = mods->reviewing
            ? "Confirm applies and saves this validated selection. Back returns to editing."
            : mods->show_order ? "Confirm opens actions for the selected mod. Start validates the pending order."
            : "Confirm turns a mod on or off and loads it right away. Installs and imports turn on by themselves.";
        ui_text_draw_wrapped(x->c, &style, mm_x(x, 1080.0f), mm_y(x, 580.0f),
                             mm_s(x, 650.0f), mm_s(x, 43.0f), 5, UI_ALIGN_LEFT, message, 1.0f);
        mods_text(x, "Left / right: previous / next details", 1080.0f, 886.0f,
                  650.0f, 26.0f, MM_WHITE);
    }
}

static void store_overlay(MmCtx *x,const ModernMenuMods *mods)
{
    const PortableStore *store=mods->store;
    const unsigned count=store?store->count:0u;
    const PortableStoreEntry *entry=store&&mods->store_cursor<count?&store->entries[mods->store_cursor]:NULL;
    const PortableModCatalogEntry *installed=entry?mods_entry(portable_mod_manager_catalog(mods->manager),entry->id):NULL;
    const char *install_label=installed?(strcmp(installed->manifest.version,entry->version)?"Update / replace ZIP":"Reinstall ZIP"):"Install ZIP";
    mods_text(x,store?store->name:"Store",176,294,760,34,MM_WHITE);
    if(mods->store_details&&entry){
        mods_text(x,entry->name,176,385,760,38,MM_YELLOW);
        char identity[256];snprintf(identity,sizeof identity,"%s  /  %s",entry->author,entry->version);
        mods_text(x,identity,176,450,760,28,MM_WHITE);
        snprintf(identity,sizeof identity,"%u bytes  /  %s",entry->size,entry->license);
        mods_text(x,identity,176,508,760,26,MM_WHITE);
        mods_text(x,portable_store_compatible(entry)?"Compatible with this game":"Incompatible game version",176,570,760,28,MM_WHITE);
        mm_tab(x,164,660,812,76,install_label,NULL,mods->store_action==0u,1,MM_ICON_NONE,0,0);
        mm_tab(x,164,754,812,76,"Remove installed ZIP",NULL,mods->store_action==1u,1,MM_ICON_NONE,0,0);
    }else{
        const unsigned first=mods->store_cursor>=6u?mods->store_cursor-5u:0u;
        static const char *const actions[]={"Next store","Refresh catalog","Add store from clipboard","Forget selected store"};
        for(unsigned row=0;row<6u&&first+row<count+4u;++row){
            const unsigned i=first+row;
            const PortableModCatalogEntry *local=i<count?mods_entry(portable_mod_manager_catalog(mods->manager),store->entries[i].id):NULL;
            const char *state=i<count?(local?"Installed":"Available"):NULL;
            mm_tab(x,164,338+(float)row*84,812,76,i<count?store->entries[i].name:actions[i-count],state,
                i==mods->store_cursor,1,MM_ICON_LIST,0,0);
        }
    }
    mods_text(x,mods->store_busy?"Working...":store&&store->offline?"Saved catalog / offline":"Store details",1080,294,650,34,MM_WHITE);
    UiTextStyle style=ui_text_style(UI_FONT_BOLD,mm_s(x,28),MM_WHITE);
    const char *description=entry?entry->description:
        "Browse approved examples offline, or add a publisher's index. Copy an HTTPS index URL then choose Add store, or drop a local index.json here. Up to four added stores are saved.";
    char page[MODS_DETAIL_PAGE_BYTES+1u];
    if(mods->store_details&&entry){
        const unsigned pages=mods_detail_pages(description,NULL);
        const unsigned selected=mods->detail_page%pages;
        size_t start=0;
        for(unsigned i=0;i<selected;++i)start=mods_detail_end(description,strlen(description),start);
        const size_t end=mods_detail_end(description,strlen(description),start);
        memcpy(page,description+start,end-start);page[end-start]='\0';description=page;
        char label[80];snprintf(label,sizeof label,"Description %u/%u  /  Left-right to page",selected+1u,pages);
        mods_text(x,label,1080,650,650,25,MM_WHITE);
        mods_text(x,entry->id,176,612,760,25,MM_WHITE);
    }
    ui_text_draw_wrapped(x->c,&style,mm_x(x,1080),mm_y(x,354),mm_s(x,650),mm_s(x,38),9,UI_ALIGN_LEFT,description,1);
    if(mods->notice&&mods->notice[0]){
        style=ui_text_style(UI_FONT_BOLD,mm_s(x,28),mods->notice_error?MM_YELLOW:MM_WHITE);
        ui_text_draw_wrapped(x->c,&style,mm_x(x,1080),mm_y(x,718),mm_s(x,650),mm_s(x,38),5,UI_ALIGN_LEFT,mods->notice,1);
    }
    const MmHint hints[]={{MM_BUTTON_B,"Back"},{MM_BUTTON_DPAD,"Choose"},{MM_BUTTON_A,mods->store_details?"Select action":"Details / select"}};
    mm_footer(x,mods->store_busy?"Please wait for the current transfer (30 seconds maximum)":NULL,hints,mods->store_busy?0u:3u);
}

void mm_mods_overlay(MmCtx *x, const ModernMenuMods *mods)
{
    mm_panel(x, 140.0f, 252.0f, 860.0f, 680.0f, 30.0f, 1.0f);
    mm_panel(x, 1040.0f, 252.0f, 740.0f, 680.0f, 30.0f, 1.0f);
    if (mods == NULL)
    {
        mods_text(x, "Mod manager unavailable", 176.0f, 390.0f, 760.0f, 36.0f, MM_WHITE);
        const MmHint hints[] = {{MM_BUTTON_B, "Back"}, {MM_BUTTON_LR, "Category"}};
        mm_footer(x, NULL, hints, 2u);
        return;
    }
    if(mods->store_open){store_overlay(x,mods);return;}
    if (mods->reviewing == 3u) {
        static const char *const choices[] = {"Apply changes", "Discard changes", "Keep editing"};
        mods_text(x,"Unsaved mod changes",176.0f,294.0f,760.0f,34.0f,MM_WHITE);
        for (unsigned i = 0u; i < 3u; ++i)
            mm_tab(x,164.0f,398.0f+(float)i*112.0f,812.0f,92.0f,choices[i],NULL,
                i == mods->cursor,1.0f,MM_ICON_NONE,0u,0);
        mods_text(x,"Choose what happens to your selection",1080.0f,390.0f,650.0f,30.0f,MM_YELLOW);
        mods_text(x,"Apply validates before saving.",1080.0f,458.0f,650.0f,28.0f,MM_WHITE);
        const MmHint hints[] = {{MM_BUTTON_B,"Keep editing"},{MM_BUTTON_DPAD,"Choose"},{MM_BUTTON_A,"Select"}};
        mm_footer(x,NULL,hints,3u);
        return;
    }
    const PortableModCatalog *catalog = portable_mod_manager_catalog(mods->manager);
    const size_t count = mods->show_order ? portable_mod_manager_count(mods->manager)
        : portable_mod_menu_row_count(mods->manager, mods->advanced != 0u);
    const size_t selected = count ? (mods->cursor < count ? mods->cursor : count - 1u) : 0u;
    mods_list(x, mods, catalog, count, selected);
    mods_details(x, mods);
    if (mods->reviewing)
    {
        const MmHint hints[] = {{MM_BUTTON_B, "Cancel"}, {MM_BUTTON_DPAD, "Details"},
                               {MM_BUTTON_A, "Apply"}};
        mm_footer(x, NULL, hints, 3u);
    }
    else if (mods->action_open)
    {
        const MmHint hints[] = {{MM_BUTTON_B, "Back"}, {MM_BUTTON_DPAD, "Action"}, {MM_BUTTON_A, "Select"}};
        mm_footer(x, NULL, hints, 3u);
    }
    else
    {
        const char *id = !mods->show_order && catalog != NULL && selected < catalog->count
            ? catalog->entries[selected].id : NULL;
        const char *confirm = mods->show_order ? "Actions" : id == NULL ? "Select"
            : portable_mod_manager_index(mods->manager, id) >= 0 ? "Turn off" : "Turn on";
        if (mods->show_order)
        {
            const MmHint hints[] = {{MM_BUTTON_B, "Back"}, {MM_BUTTON_START, "Review"},
                                    {MM_BUTTON_A, confirm}};
            mm_footer(x, NULL, hints, 3u);
        }
        else
        {
            const MmHint hints[] = {{MM_BUTTON_B, "Back"}, {MM_BUTTON_LR, "Category"},
                                    {MM_BUTTON_A, confirm}};
            mm_footer(x, NULL, hints, 3u);
        }
    }
}
