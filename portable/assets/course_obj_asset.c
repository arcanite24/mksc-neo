#include "course_obj_asset.h"

#include <string.h>

static uint16_t read_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static int span_is_valid(size_t data_size, size_t offset, size_t length)
{
    return offset <= data_size && length <= data_size - offset;
}

static int decode_lz77(
    const uint8_t *data,
    size_t data_size,
    size_t source_offset,
    uint8_t *destination,
    size_t destination_capacity,
    size_t *out_size)
{
    size_t source;
    size_t produced = 0;
    uint32_t expected;

    if (data == NULL || destination == NULL || out_size == NULL ||
        !span_is_valid(data_size, source_offset, 4))
        return PORTABLE_COURSE_OBJ_INVALID;
    if (data[source_offset] != 0x10)
        return PORTABLE_COURSE_OBJ_INVALID;
    expected = (uint32_t)data[source_offset + 1] |
               ((uint32_t)data[source_offset + 2] << 8) |
               ((uint32_t)data[source_offset + 3] << 16);
    if (expected == 0)
        return PORTABLE_COURSE_OBJ_INVALID;
    if (expected > destination_capacity)
        return PORTABLE_COURSE_OBJ_INVALID;

    source = source_offset + 4;
    while (produced < expected)
    {
        uint8_t flags;
        unsigned bit;

        if (!span_is_valid(data_size, source, 1))
            return PORTABLE_COURSE_OBJ_INVALID;
        flags = data[source++];
        for (bit = 0; bit < 8 && produced < expected; ++bit)
        {
            if ((flags & (uint8_t)(0x80u >> bit)) == 0)
            {
                if (!span_is_valid(data_size, source, 1))
                    return PORTABLE_COURSE_OBJ_INVALID;
                destination[produced++] = data[source++];
            }
            else
            {
                uint8_t first;
                uint8_t second;
                size_t distance;
                size_t length;
                size_t copy;

                if (!span_is_valid(data_size, source, 2))
                    return PORTABLE_COURSE_OBJ_INVALID;
                first = data[source++];
                second = data[source++];
                distance = (((size_t)first & 0x0fu) << 8) |
                           (size_t)second;
                distance += 1u;
                length = ((size_t)first >> 4) + 3u;
                if (distance > produced || length > expected - produced)
                    return PORTABLE_COURSE_OBJ_INVALID;
                for (copy = 0; copy < length; ++copy)
                {
                    destination[produced] = destination[produced - distance];
                    ++produced;
                }
            }
        }
    }
    *out_size = produced;
    return PORTABLE_COURSE_OBJ_OK;
}

int portable_course_obj_asset_build(const PortableTrackAsset *track,
    PortableCourseObjAsset *asset)
{
    if (!track || !asset || !track->data || track->size < 0xedu)
        return PORTABLE_COURSE_OBJ_INVALID;
    const uint8_t *data=track->data;
    if (data[0xec]) return PORTABLE_COURSE_OBJ_UNSUPPORTED;
    PortableCourseObjAsset next={0};
    next.resource=data; next.resource_bytes=track->size;
    const unsigned fields[3]={0x24u,0x08u,0xd4u};
    for (unsigned i=0;i<3;i++) {
        next.relative_refs[i]=read_le32(data+fields[i]);
        if (next.relative_refs[i]>=track->size) return PORTABLE_COURSE_OBJ_INVALID;
    }
    size_t table=read_le32(data+0xe4);
    if (table) {
        if (!(data[3]&4u)) return PORTABLE_COURSE_OBJ_UNSUPPORTED;
        if (!span_is_valid(track->size,table,4)) return PORTABLE_COURSE_OBJ_INVALID;
        for (unsigned i=0;i<2;i++) {
            size_t relative=read_le16(data+table+i*2);
            if (!relative) continue;
            if (relative>track->size-table) return PORTABLE_COURSE_OBJ_INVALID;
            size_t decoded=0;
            int status=decode_lz77(data,track->size,table+relative,next.blocks[i],4096,&decoded);
            if (status) return status;
            if (decoded!=4096u) return PORTABLE_COURSE_OBJ_UNSUPPORTED;
            next.block_bytes[i]=decoded;
        }
    }
    size_t palette=read_le32(data+0xe8);
    if (palette) {
        if (!span_is_valid(track->size,palette,96)) return PORTABLE_COURSE_OBJ_INVALID;
        memcpy(next.palette,data+palette,96);next.palette_bytes=96;
    }
    *asset=next;
    return PORTABLE_COURSE_OBJ_OK;
}
