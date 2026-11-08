#include "mod_menu_renderer.h"
#include "menu_font.h"
#include "frontend_image_renderer.h"
#include <stdio.h>
#include <string.h>

/* Keep the imported 8x16 glyphs intact. Only strings are clipped; all
 * diagnostic text remains reachable through the separate detail pages. */
static void bounded_text(const PortableAssetPackView *pack, PortableSoftwareFrame *frame,
                         int x, int y, const char *text, size_t limit)
{
    char line[28];
    if (text == NULL)
        return;
    if (limit > 27u)
        limit = 27u;
    size_t length = 0u;
    while (length < limit && text[length] != '\0') {
        line[length] = text[length];
        ++length;
    }
    line[length] = '\0';
    portable_menu_text(pack, frame, x, y, line);
}

static const PortableModCatalogEntry *catalog_entry(const PortableModCatalog *catalog,
                                                   const char *id)
{
    if (catalog != NULL && id != NULL)
        for (size_t index = 0u; index < catalog->count; ++index)
            if (strcmp(catalog->entries[index].id, id) == 0)
                return &catalog->entries[index];
    return NULL;
}

enum { DETAIL_CAPACITY = PORTABLE_MOD_DETAIL_CAPACITY,
       DETAIL_PAGE_BYTES = PORTABLE_MOD_DETAIL_PAGE_BYTES };

size_t portable_mod_menu_row_count(const PortableModManager *manager, int advanced)
{
    const PortableModCatalog *catalog = portable_mod_manager_catalog(manager);
    size_t count = catalog != NULL ? catalog->count : 0u;
    if (count > PORTABLE_MOD_CATALOG_LIMIT)
        count = PORTABLE_MOD_CATALOG_LIMIT;
    return count + PORTABLE_MOD_MENU_ACTIONS + (advanced ? PORTABLE_MOD_MENU_ADVANCED_ACTIONS : 0u);
}

PortableModMenuRow portable_mod_menu_row(const PortableModManager *manager, size_t index, int advanced)
{
    const size_t rows = portable_mod_menu_row_count(manager, advanced);
    const size_t mods = rows - PORTABLE_MOD_MENU_ACTIONS - (advanced ? PORTABLE_MOD_MENU_ADVANCED_ACTIONS : 0u);
    if (index < mods)
        return PORTABLE_MOD_ROW_MOD;
    if (index >= rows)
        return PORTABLE_MOD_ROW_ADVANCED;
    return (PortableModMenuRow)(PORTABLE_MOD_ROW_INSTALL + (index - mods));
}

const char *portable_mod_menu_row_label(PortableModMenuRow row, int advanced, int classic)
{
    switch (row) {
    case PORTABLE_MOD_ROW_INSTALL: return classic ? "INSTALL ZIP..." : "Install ZIP...";
    case PORTABLE_MOD_ROW_IMPORT: return classic ? "IMPORT GAME ROM..." : "Import from another game...";
    case PORTABLE_MOD_ROW_STORE: return classic ? "STORE..." : "Store...";
    case PORTABLE_MOD_ROW_WORKSHOP: return classic ? "COURSE WORKSHOP..." : "Course Workshop...";
    case PORTABLE_MOD_ROW_ADVANCED:
        return classic ? (advanced ? "HIDE ADVANCED" : "ADVANCED...") : (advanced ? "Hide advanced" : "Advanced...");
    case PORTABLE_MOD_ROW_ORDER: return classic ? "LOAD ORDER..." : "Load order and review...";
    case PORTABLE_MOD_ROW_RELOAD: return classic ? "RELOAD FROM DISK" : "Reload from disk";
    case PORTABLE_MOD_ROW_FOLDER: return classic ? "ADD MOD FOLDER..." : "Add mod folder...";
    case PORTABLE_MOD_ROW_MOD: break;
    }
    return "";
}

static const char *row_help(PortableModMenuRow row, int advanced)
{
    switch (row) {
    case PORTABLE_MOD_ROW_INSTALL: return "Choose or drop a mod ZIP. It is checked, installed and turned on in one step.";
    case PORTABLE_MOD_ROW_IMPORT: return "Convert a game you own into a mod and turn it on: Mario Kart DS or Mario Kart 64 (US), raw or zipped. Drop a ROM here too. Choose again to update it.";
    case PORTABLE_MOD_ROW_STORE: return "Browse Store: built-in examples and added catalogs. Downloads are verified, installed and turned on.";
    case PORTABLE_MOD_ROW_WORKSHOP: return "Create or open a 3D course, place snapped pieces, save a mod and test drive.";
    case PORTABLE_MOD_ROW_ADVANCED: return advanced ? "Hide the advanced tools." : "Show load order review, Reload from disk and extra mod folders.";
    case PORTABLE_MOD_ROW_ORDER: return "Reorder or disable enabled mods. Changes wait until Start validates them and you confirm.";
    case PORTABLE_MOD_ROW_RELOAD: return "Reload current selection from disk. F5 also reloads in menus. Validation failures keep active content.";
    case PORTABLE_MOD_ROW_FOLDER: return "Remember an additional mod folder in settings. Up to four extra folders; duplicate IDs are errors.";
    case PORTABLE_MOD_ROW_MOD: break;
    }
    return "";
}

unsigned portable_mod_menu_detail(const PortableModManager *manager, unsigned cursor,
                                int show_order, int advanced, const char *notice,
                                char text[DETAIL_CAPACITY])
{
    const PortableModCatalog *catalog = portable_mod_manager_catalog(manager);
    const size_t count = show_order ? portable_mod_manager_count(manager)
                                   : (catalog != NULL ? catalog->count : 0u);
    const size_t selected = cursor;
    const char *id = selected >= count ? NULL : (show_order
        ? portable_mod_manager_id(manager, selected) : catalog->entries[selected].id);
    const PortableModCatalogEntry *entry = catalog_entry(catalog, id);
    char preview[PORTABLE_MOD_ERROR_CAPACITY] = {0};
    if (notice == NULL)
        notice = portable_mod_manager_notice(manager);
    if ((notice == NULL || notice[0] == '\0') && id != NULL) {
        const int rank = portable_mod_manager_index(manager, id);
        if (rank >= 0)
            (void)portable_mod_manager_preview(manager, (size_t)rank, preview, sizeof preview);
        else if (entry != NULL && !entry->valid)
            snprintf(preview, sizeof preview, "%s", entry->error);
        else if (entry != NULL && entry->manifest.dependency_count != 0u)
            snprintf(preview, sizeof preview, "Requires %s; turning it on enables that too", entry->manifest.depends[0]);
        else if (entry != NULL && entry->manifest.after_count != 0u)
            snprintf(preview, sizeof preview, "Load after %s", entry->manifest.load_after[0]);
        else
            snprintf(preview, sizeof preview, "Off. Turn it on to load it now");
        notice = preview;
    }
    /* Manifest fields and loader notices are bounded by their model schemas.
     * Keep the complete identity here even when the list label is clipped. */
    if (id == NULL && (notice == NULL || notice[0] == '\0'))
        notice = show_order ? "No mods enabled" : row_help(portable_mod_menu_row(manager, selected, advanced), advanced);
    snprintf(text, DETAIL_CAPACITY, "%s%s%s%s%s%s", id != NULL ? "ID " : "",
             id != NULL ? id : "", entry != NULL && entry->valid ? " VERSION " : "",
             entry != NULL && entry->valid ? entry->manifest.version : "",
             id != NULL ? " - " : "", notice != NULL ? notice : "");
    const size_t length = strlen(text);
    return length == 0u ? 1u : (unsigned)((length + DETAIL_PAGE_BYTES - 1u) / DETAIL_PAGE_BYTES);
}

unsigned portable_mod_menu_pages(const PortableModManager *manager, unsigned cursor,
                                 int show_order, int advanced, const char *notice)
{
    char detail[DETAIL_CAPACITY];
    return portable_mod_menu_detail(manager, cursor, show_order, advanced, notice, detail);
}

PortableFrontendPresentationStatus portable_mod_menu_render(const PortableAssetPackView *pack,
                              const PortableFrontendFlow *flow,
                              const PortableModManager *manager, unsigned cursor,
                              int show_order, int advanced, const char *notice, unsigned detail_page, int reviewing, PortableSoftwareFrame *frame)
{
    if (frame == NULL || frame->pixels == NULL || frame->width < 240u || frame->height != 160u)
        return PORTABLE_FRONTEND_PRESENTATION_INVALID_ARGUMENT;
    if (frame->width > 240u) {
        if (flow == NULL)
            return PORTABLE_FRONTEND_PRESENTATION_INVALID_ARGUMENT;
        const PortableFrontendPresentationStatus status=portable_menu_background_render(
            pack,flow,PORTABLE_MENU_PATTERN,PORTABLE_MENU_BACKGROUND,frame);
        if (status != PORTABLE_FRONTEND_PRESENTATION_OK)
            return status;
    }
    PortableSoftwareFrame centered=portable_software_frame_native_view(frame,PORTABLE_FRAME_CENTER);
    frame=&centered;
    const PortableFrontendPresentationStatus panel=portable_menu_panel(pack,frame,0,0,240u,160u);
    if (panel != PORTABLE_FRONTEND_PRESENTATION_OK)
        return panel;
    if (reviewing == 3) {
        static const char *const choices[] = {"APPLY CHANGES", "DISCARD CHANGES", "KEEP EDITING"};
        bounded_text(pack,frame,12,4,"UNSAVED MOD CHANGES",27u);
        for (unsigned i = 0u; i < 3u; ++i) {
            bounded_text(pack,frame,12,32+(int)i*24,i == cursor ? ">" : " ",1u);
            bounded_text(pack,frame,24,32+(int)i*24,choices[i],25u);
        }
        bounded_text(pack,frame,12,126,"APPLY VALIDATES BEFORE SAVE",27u);
        bounded_text(pack,frame,12,142,"A SELECT  B KEEP EDITING",27u);
        return PORTABLE_FRONTEND_PRESENTATION_OK;
    }
    const PortableModCatalog *catalog = portable_mod_manager_catalog(manager);
    const size_t count = show_order ? portable_mod_manager_count(manager)
                                   : portable_mod_menu_row_count(manager, advanced);
    const size_t selected = count == 0u ? 0u : (cursor < count ? cursor : count - 1u);
    const size_t first = selected >= 4u ? selected - 3u : 0u;
    for (size_t row = 0u; row < 4u && first + row < count; ++row) {
        const size_t index = first + row;
        const char *id = show_order ? portable_mod_manager_id(manager, index)
                                   : (catalog!=NULL && index<catalog->count?catalog->entries[index].id:NULL);
        const PortableModCatalogEntry *entry = catalog_entry(catalog, id);
        const int rank = portable_mod_manager_index(manager, id);
        const char *label = entry != NULL && entry->valid && entry->manifest.name[0] != '\0'
                                ? entry->manifest.name : id!=NULL?id:
                                  portable_mod_menu_row_label(portable_mod_menu_row(manager,index,advanced),advanced,1);
        char value[11];
        if (show_order)
            snprintf(value, sizeof value, "%u", (unsigned)index + 1u);
        else if(id==NULL)
            value[0]='\0';
        else if (entry == NULL || !entry->valid)
            snprintf(value, sizeof value, "BAD");
        else if (rank >= 0)
            snprintf(value, sizeof value, "%u", (unsigned)rank + 1u);
        else
            snprintf(value, sizeof value, "OFF");
        const int y = 24 + (int)row * 16;
        bounded_text(pack, frame, 12, y, index == selected ? ">" : " ", 1u);
        bounded_text(pack, frame, 24, y, label, 21u);
        bounded_text(pack, frame, 200, y, value, 3u);
    }
    if (count == 0u)
        bounded_text(pack, frame, 12, 24, "NO MODS ENABLED", 36u);
    char detail[DETAIL_CAPACITY], page_label[48];
    const unsigned pages = portable_mod_menu_detail(manager, cursor, show_order, advanced, notice, detail);
    const unsigned page = detail_page % pages;
    snprintf(page_label, sizeof page_label, "%s  DETAILS %u/%u",
             reviewing ? "REVIEW" : show_order ? "LOAD ORDER" : "MODS", page + 1u, pages);
    bounded_text(pack, frame, 12, 4, page_label, 27u);
    const size_t offset = (size_t)page * DETAIL_PAGE_BYTES;
    const size_t length = strlen(detail);
    /* Fixed character slices make every byte reachable exactly once. Wrapping
     * at spaces here would otherwise skip text at a page boundary. */
    for (unsigned row = 0u; row < 2u; ++row) {
        const size_t start = offset + row * 27u;
        if (start < length)
            bounded_text(pack, frame, 12, 90 + (int)row * 16, detail + start, 27u);
    }
    bounded_text(pack, frame, 12, 126,
                 reviewing ? "A/START APPLY  B EDIT"
                           : show_order ? "A REMOVE START APPLY B BACK"
                                        : "A ON/OFF OR SELECT B BACK", 27u);
    bounded_text(pack, frame, 12, 142,
                 reviewing ? "L/R DETAILS  U/D REVIEW"
                           : show_order ? "L/R INFO LB/RB MOVE" : "L/R INFO  TAB LOAD ORDER", 27u);
    return PORTABLE_FRONTEND_PRESENTATION_OK;
}
