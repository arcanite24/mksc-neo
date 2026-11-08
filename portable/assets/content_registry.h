#ifndef PORTABLE_CONTENT_REGISTRY_H
#define PORTABLE_CONTENT_REGISTRY_H

#include "asset_pack.h"

#define PORTABLE_SOURCE_DRIVER_CLASS_COUNT 8u

typedef enum
{
    PORTABLE_CONTENT_OK = 0,
    PORTABLE_CONTENT_INVALID_ARGUMENT,
    PORTABLE_CONTENT_NOT_FOUND,
    PORTABLE_CONTENT_BAD_MANIFEST,
    PORTABLE_CONTENT_MISSING_REFERENCE,
    PORTABLE_CONTENT_INCOMPATIBLE
} PortableContentStatus;

typedef struct
{
    uint32_t id,revision,rules,route,world,material,road,sky,minimap,hud,collision;
    uint32_t expected_recovered_ticks,expected_recovered_race_ticks;
    uint32_t expected_demo_ticks,expected_demo_left_ticks,expected_demo_right_ticks;
} PortableCourseContent;

typedef struct
{
    uint32_t id,revision,graphics,motion,character_class;
} PortableCharacterContent;

/* Selectable drivers added by kind79 roster sections (DRV1). Their IDs are
 * the character manifest IDs; they keep their manifest's seat class but never
 * answer class lookups, which stay with each class's stock driver. */
#define PORTABLE_DRIVER_ROSTER_CAPACITY 56u
#define PORTABLE_DRIVER_ROSTER_MAX_ID 255u

typedef struct
{
    uint32_t character_id;
    uint8_t art_slot; /* Menu grid slot (0..8, not 4) its art is placed for. */
    uint8_t voice_driver; /* Stock driver ID 1..8 lending voices, horn and engine; 0: the seat's. */
} PortableDriverRosterEntry;

typedef struct
{
    PortableDriverRosterEntry entries[PORTABLE_DRIVER_ROSTER_CAPACITY];
    size_t count;
} PortableDriverRoster;

typedef struct
{
    PortableCourseContent course;
    PortableCharacterContent character;
} PortableContentSelection;

PortableContentStatus portable_content_registry_select(
    const PortableAssetPackView *pack,uint32_t course_id,uint32_t character_id,
    PortableContentSelection *out);
PortableContentStatus portable_content_registry_find_character_class(
    const PortableAssetPackView *pack,uint32_t character_class,
    PortableCharacterContent *out);
PortableContentStatus portable_content_registry_character(
    const PortableAssetPackView *pack,uint32_t character_id,
    PortableCharacterContent *out);
/* A saved driver: the class's stock driver, or roster_driver_id (nonzero)
 * when that roster driver still exists with the same seat class. */
PortableContentStatus portable_content_registry_saved_driver(
    const PortableAssetPackView *pack,uint32_t character_class,
    uint32_t roster_driver_id,PortableCharacterContent *out);
/* Rows of every roster section, ascending section content ID then row order. */
PortableContentStatus portable_content_registry_driver_roster(
    const PortableAssetPackView *pack,PortableDriverRoster *out);
/* The stock driver whose voices, horn and engine a driver uses: a roster
 * row's voice_driver, else its seat class's stock driver (itself when stock). */
PortableContentStatus portable_content_registry_voice_driver(
    const PortableAssetPackView *pack,uint32_t character_id,
    PortableCharacterContent *out);
/* Kind80/ID1 v1 import stamp: u32 magic 0x31504d49 ("IMP1"), u32 importer
 * version. Returns that version, or 0 for packs from importers before the
 * stamp (and for malformed stamps). Compare with MKSC_ROM_IMPORT_VERSION. */
#define PORTABLE_IMPORT_STAMP_MAGIC UINT32_C(0x31504d49)
uint32_t portable_content_import_version(const PortableAssetPackView *pack);
const char *portable_content_status_name(PortableContentStatus status);

#endif
