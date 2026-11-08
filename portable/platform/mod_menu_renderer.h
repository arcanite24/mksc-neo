#ifndef PORTABLE_MOD_MENU_RENDERER_H
#define PORTABLE_MOD_MENU_RENDERER_H

/* Rows of the Installed list: the mods, then everyday actions, then an
 * Advanced row that shows or hides load order, Reload and extra folders. */
typedef enum {
    PORTABLE_MOD_ROW_MOD, PORTABLE_MOD_ROW_INSTALL, PORTABLE_MOD_ROW_IMPORT,
    PORTABLE_MOD_ROW_STORE, PORTABLE_MOD_ROW_WORKSHOP, PORTABLE_MOD_ROW_ADVANCED,
    PORTABLE_MOD_ROW_ORDER, PORTABLE_MOD_ROW_RELOAD, PORTABLE_MOD_ROW_FOLDER
} PortableModMenuRow;
#define PORTABLE_MOD_MENU_ACTIONS 5u
#define PORTABLE_MOD_MENU_ADVANCED_ACTIONS 3u

#include "mod_manager.h"
#include "software_frame.h"
#include "frontend_presentation.h"

size_t portable_mod_menu_row_count(const PortableModManager *manager, int advanced);
PortableModMenuRow portable_mod_menu_row(const PortableModManager *manager, size_t index, int advanced);
/* Classic uses the imported uppercase menu font's wording. */
const char *portable_mod_menu_row_label(PortableModMenuRow row, int advanced, int classic);

/* Page count includes complete selected identity and validation diagnostics. */
enum { PORTABLE_MOD_DETAIL_CAPACITY = 512u, PORTABLE_MOD_DETAIL_PAGE_BYTES = 54u };
unsigned portable_mod_menu_detail(const PortableModManager *manager, unsigned cursor,
    int show_order, int advanced, const char *notice, char text[PORTABLE_MOD_DETAIL_CAPACITY]);
unsigned portable_mod_menu_pages(const PortableModManager *manager, unsigned cursor,
    int show_order, int advanced, const char *notice);

/* Immutable native-layout overlay; cursor indexes the selected view. */
PortableFrontendPresentationStatus portable_mod_menu_render(const PortableAssetPackView *pack,
                              const PortableFrontendFlow *flow,
    const PortableModManager *manager, unsigned cursor, int show_order, int advanced,
    const char *notice, unsigned detail_page, int reviewing, PortableSoftwareFrame *frame);

#endif
