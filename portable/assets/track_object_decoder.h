#ifndef MKSC_PORTABLE_TRACK_OBJECT_DECODER_H
#define MKSC_PORTABLE_TRACK_OBJECT_DECODER_H

#include <stddef.h>
#include <stdint.h>

enum
{
    PORTABLE_TRACK_OBJECT_DECODER_OK = 0,
    PORTABLE_TRACK_OBJECT_DECODER_INVALID_ARGUMENT = 1,
    PORTABLE_TRACK_OBJECT_DECODER_OUT_OF_BOUNDS = 2,
    PORTABLE_TRACK_OBJECT_DECODER_UNSUPPORTED_CLASS = 3,
    PORTABLE_TRACK_OBJECT_DECODER_UNSUPPORTED_OBJECT = 4,
};

enum
{
    PORTABLE_TRACK_OBJECT_DECODER_MIN_CLASS = 4,
    PORTABLE_TRACK_OBJECT_DECODER_MAX_CLASS = 28,
    PORTABLE_TRACK_OBJECT_DECODER_DEFAULT_TABLE_OFFSET = 0x0f1008,
    PORTABLE_TRACK_OBJECT_DECODER_CLASS_TABLE_OFFSET = 0x0f1054,
    PORTABLE_TRACK_OBJECT_DECODER_DEFAULT_TABLE_BYTES = 0x44,
};

typedef struct
{
    uint8_t object_class;
    uint8_t raw_type;
    uint32_t table_offset;
    uint32_t source_code;
} PortableTrackObjectDecode;

typedef enum
{
    PORTABLE_TRACK_OBJECT_ACTOR_INIT_UNSUPPORTED = 0,
    PORTABLE_TRACK_OBJECT_ACTOR_INIT_GENERIC_EXTENDED = 1,
} PortableTrackObjectActorInitKind;

typedef enum
{
    PORTABLE_TRACK_OBJECT_UPDATE_NONE = 0,
    PORTABLE_TRACK_OBJECT_UPDATE_8059E00 = 1,
} PortableTrackObjectUpdateKind;

typedef enum
{
    PORTABLE_TRACK_OBJECT_COLLISION_NONE = 0,
    PORTABLE_TRACK_OBJECT_COLLISION_8059E44 = 1,
} PortableTrackObjectCollisionKind;

typedef struct
{
    PortableTrackObjectActorInitKind kind;
    uint32_t source_code;
    uint16_t source_actor_type;
    uint8_t size_x;
    uint8_t size_y;
    uint8_t size_z;
    uint16_t source_flags;
    uint16_t draw_scale;
    PortableTrackObjectUpdateKind update_kind;
    PortableTrackObjectCollisionKind collision_kind;
    uint8_t active;
    uint8_t timer;
} PortableTrackObjectActorInit;

/* Reproduce sub_8053DC4's class/raw-type table lookup. */
int portable_track_object_decode(
    const uint8_t *rom,
    size_t rom_size,
    uint8_t object_class,
    uint8_t raw_type,
    PortableTrackObjectDecode *out);

/* loadtrack_loadObjects only calls sub_8053220 for these decoded families. */
int portable_track_object_uses_runtime_map_alias(uint32_t source_code);

/* Describe the concrete generic actor initializer used by Peach code 0x000c0002. */
int portable_track_object_actor_init(
    uint32_t source_code,
    PortableTrackObjectActorInit *out);

#endif
