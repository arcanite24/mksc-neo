#ifndef PORTABLE_TEXT_DATA_H
#define PORTABLE_TEXT_DATA_H
#include "json_reader.h"
#include "mod_manifest.h"
#include "asset_pack.h"
#define PORTABLE_TEXT_DATA_LIMIT 64u
typedef struct { uint32_t id,color_rgb; char value[59]; } PortableTextOverride;
typedef struct { PortableTextOverride records[PORTABLE_TEXT_DATA_LIMIT]; unsigned count; } PortableTextData;
int portable_ui_strings_parse(PortableJsonReader *reader,PortableTextData *out,char *error);
int portable_text_data_parse(PortableJsonReader *reader,PortableTextData *out,char *error);
/* Replaces pixels on the existing canvas, retaining anchors and dimensions. */
int portable_text_data_render(const PortableTextOverride *record,const PortableAssetSectionView *source,uint8_t *bytes,char *error);
#endif
