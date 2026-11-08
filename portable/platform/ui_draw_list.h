#ifndef PORTABLE_UI_DRAW_LIST_H
#define PORTABLE_UI_DRAW_LIST_H

#include "ui_draw.h"

/* One bounded allocation owns commands and all borrowed draw payloads. Limits
 * include bookkeeping; a failed list exposes no consumable partial commands. */
typedef struct UiDrawList UiDrawList;
UiDrawList *ui_draw_list_create(size_t command_limit, size_t byte_limit);
void ui_draw_list_reset(UiDrawList *list);
void ui_draw_list_close(UiDrawList *list);
UiCanvasSink *ui_draw_list_sink(UiDrawList *list);
size_t ui_draw_list_count(const UiDrawList *list);
size_t ui_draw_list_bytes(const UiDrawList *list);
const UiDrawCommand *ui_draw_list_command(const UiDrawList *list, size_t index);
const char *ui_draw_list_error(const UiDrawList *list);

#endif
