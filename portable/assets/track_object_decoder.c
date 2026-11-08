#include "track_object_decoder.h"

#include <stdint.h>

static const uint32_t class_table_offsets[] = {
    0x0f1054, 0x0f1060, 0x0f1088, 0x0f10a0, 0x0f1118,
    0x0f1130, 0x0f1154, 0x0f10a0, 0x0f1174, 0x0f1188,
    0x0f11a0, 0x0f10a0, 0x0f11c0, 0x0f11d8, 0x0f1204,
    0x0f1228, 0x0f125c, 0x0f11ec, 0x0f10a0, 0x0f126c,
    0x0f1008, 0x0f10a0, 0x0f1154, 0x0f1008, 0x0f1054,
};

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static int source_range_valid(size_t rom_size,
                              uint32_t offset,
                              size_t byte_count)
{
    return (size_t)offset <= rom_size &&
           byte_count <= rom_size - (size_t)offset;
}

int portable_track_object_decode(
    const uint8_t *rom,
    size_t rom_size,
    uint8_t object_class,
    uint8_t raw_type,
    PortableTrackObjectDecode *out)
{
    uint32_t table_offset;
    size_t class_index;

    if (rom == NULL || out == NULL)
        return PORTABLE_TRACK_OBJECT_DECODER_INVALID_ARGUMENT;
    if (object_class < PORTABLE_TRACK_OBJECT_DECODER_MIN_CLASS ||
        object_class > PORTABLE_TRACK_OBJECT_DECODER_MAX_CLASS)
        return PORTABLE_TRACK_OBJECT_DECODER_UNSUPPORTED_CLASS;

    class_index = (size_t)object_class -
                  PORTABLE_TRACK_OBJECT_DECODER_MIN_CLASS;
    table_offset = class_table_offsets[class_index];
    if (raw_type > 0x7f)
    {
        table_offset = PORTABLE_TRACK_OBJECT_DECODER_DEFAULT_TABLE_OFFSET +
                       (uint32_t)raw_type * 4u - 0x200u;
        if (!source_range_valid(
                rom_size, table_offset, sizeof(uint32_t)) ||
            table_offset + sizeof(uint32_t) >
                PORTABLE_TRACK_OBJECT_DECODER_DEFAULT_TABLE_OFFSET +
                    PORTABLE_TRACK_OBJECT_DECODER_DEFAULT_TABLE_BYTES)
            return PORTABLE_TRACK_OBJECT_DECODER_OUT_OF_BOUNDS;
    }
    else if (!source_range_valid(rom_size, table_offset +
                                      (uint32_t)raw_type * 4u,
                                  sizeof(uint32_t)))
        return PORTABLE_TRACK_OBJECT_DECODER_OUT_OF_BOUNDS;

    out->object_class = object_class;
    out->raw_type = raw_type;
    out->table_offset = table_offset;
    if (raw_type <= 0x7f)
        table_offset += (uint32_t)raw_type * 4u;
    out->table_offset = table_offset;
    out->source_code = read_le32(rom + table_offset);
    if (out->source_code == UINT32_C(0xffff0000) ||
        out->source_code == UINT32_C(0xfff80000) ||
        (out->source_code & UINT32_C(0xffff0000)) == UINT32_C(0xfff00000))
        return PORTABLE_TRACK_OBJECT_DECODER_UNSUPPORTED_OBJECT;
    return PORTABLE_TRACK_OBJECT_DECODER_OK;
}

int portable_track_object_uses_runtime_map_alias(uint32_t source_code)
{
    switch (source_code & UINT32_C(0xffff0000))
    {
    case UINT32_C(0x00040000):
    case UINT32_C(0x00050000):
    case UINT32_C(0x00060000):
    case UINT32_C(0x00080000):
    case UINT32_C(0x00090000):
    case UINT32_C(0x000a0000):
    case UINT32_C(0x000c0000):
    case UINT32_C(0x000e0000):
    case UINT32_C(0x00860000):
    case UINT32_C(0x00950000):
    case UINT32_C(0x00960000):
    case UINT32_C(0x00980000):
        return 1;
    default:
        return 0;
    }
}

int portable_track_object_actor_init(
    uint32_t source_code,
    PortableTrackObjectActorInit *out)
{
    if (out == NULL)
        return PORTABLE_TRACK_OBJECT_DECODER_INVALID_ARGUMENT;
    *out = (PortableTrackObjectActorInit){
        .source_code = source_code,
    };
    if (source_code != UINT32_C(0x000c0002))
    {
        out->kind = PORTABLE_TRACK_OBJECT_ACTOR_INIT_UNSUPPORTED;
        return PORTABLE_TRACK_OBJECT_DECODER_UNSUPPORTED_OBJECT;
    }

    /* sub_8053104 followed by the default sub_8059E80 branch. */
    out->kind = PORTABLE_TRACK_OBJECT_ACTOR_INIT_GENERIC_EXTENDED;
    out->source_actor_type = 0x1000;
    out->size_x = 8;
    out->size_y = 8;
    out->size_z = 8;
    out->source_flags = 1;
    out->draw_scale = 0x100;
    out->update_kind = PORTABLE_TRACK_OBJECT_UPDATE_8059E00;
    out->collision_kind = PORTABLE_TRACK_OBJECT_COLLISION_8059E44;
    out->active = 0;
    out->timer = 0;
    return PORTABLE_TRACK_OBJECT_DECODER_OK;
}
