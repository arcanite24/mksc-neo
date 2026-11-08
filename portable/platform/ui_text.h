#ifndef PORTABLE_UI_TEXT_H
#define PORTABLE_UI_TEXT_H
#include "asset_pack.h"
#define UI_STRING(id,value) +1
enum { PORTABLE_UI_TEXT_COUNT = 0
#include "../core/ui_strings.inc"
};
#undef UI_STRING
/* Borrowed immutable strings; lifetime is the effective content pack. */
typedef struct { const char *values[PORTABLE_UI_TEXT_COUNT]; } PortableUiTextBank;
const char *portable_ui_text_default(uint32_t id);
int portable_ui_text_valid(uint32_t id,const char *value);
const char *portable_ui_text_resolve(const PortableUiTextBank *bank,const char *text);
int portable_ui_text_load(const PortableAssetPackView *pack,PortableUiTextBank *bank);
#endif
