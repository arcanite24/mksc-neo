#ifndef MKSC_PORTABLE_TRACK_OBJECT_ASSET_H
#define MKSC_PORTABLE_TRACK_OBJECT_ASSET_H

#include <stddef.h>
#include <stdint.h>

#include "track_asset.h"

enum
{
    PORTABLE_TRACK_STREAM_COIN = 0x90u,
    PORTABLE_TRACK_STREAM_OBJECT = 0x8cu,
    PORTABLE_TRACK_STREAM_SECONDARY_OBJECT = 0x94u,
    PORTABLE_TRACK_STREAM_BOUNDARY = 0x98u,
    PORTABLE_TRACK_STREAM_RECORD_SIZE = 4u,
    PORTABLE_TRACK_SOURCE_COIN_FLAGS = 0xc0u,
};

typedef enum
{
    PORTABLE_TRACK_OBJECT_ASSET_OK = 0,
    PORTABLE_TRACK_OBJECT_ASSET_INVALID_ARGUMENT,
    PORTABLE_TRACK_OBJECT_ASSET_INVALID_FIELD,
    PORTABLE_TRACK_OBJECT_ASSET_INVALID_OFFSET,
    PORTABLE_TRACK_OBJECT_ASSET_TRUNCATED_STREAM,
    PORTABLE_TRACK_OBJECT_ASSET_INDEX_OUT_OF_BOUNDS,
} PortableTrackObjectAssetStatus;

/*
 * The four source lists are serialized as four-byte records and terminate
 * when the first byte is zero. The view does not copy records or retain a
 * source runtime address; it points only into the caller-owned track blob.
 */
typedef struct
{
    const uint8_t *data;
    size_t size;
    uint32_t field_offset;
    uint32_t stream_offset;
    uint32_t terminator_offset;
    uint32_t record_count;
} PortableTrackObjectStream;

typedef struct
{
    uint32_t stream_offset;
    uint8_t raw_type;
    uint8_t x;
    uint8_t y;
    uint8_t parameter;
    uint32_t source_x_q19;
    uint32_t source_y_q19;
} PortableTrackRawObject;

typedef struct
{
    uint32_t stream_offset;
    uint8_t marker;
    uint8_t x;
    uint8_t y;
    uint8_t parameter;
    int32_t world_x;
    int32_t world_y;
    uint8_t source_flags;
} PortableTrackCoinPlacement;

typedef struct
{
    uint32_t stream_offset;
    uint8_t opcode;
    uint8_t x;
    uint8_t y;
    uint8_t parameter;
} PortableTrackBoundaryCommand;

PortableTrackObjectAssetStatus portable_track_object_stream_open(
    const PortableTrackAsset *asset,
    uint32_t field_offset,
    PortableTrackObjectStream *out);

PortableTrackObjectAssetStatus portable_track_object_stream_read(
    const PortableTrackObjectStream *stream,
    uint32_t index,
    uint8_t record[PORTABLE_TRACK_STREAM_RECORD_SIZE]);

PortableTrackObjectAssetStatus portable_track_raw_object_read(
    const PortableTrackObjectStream *stream,
    uint32_t index,
    PortableTrackRawObject *out);

PortableTrackObjectAssetStatus portable_track_coin_read(
    const PortableTrackObjectStream *stream,
    uint32_t index,
    PortableTrackCoinPlacement *out);

PortableTrackObjectAssetStatus portable_track_boundary_read(
    const PortableTrackObjectStream *stream,
    uint32_t index,
    PortableTrackBoundaryCommand *out);

/* This is the exact width query performed by source sub_8032D9C. */
PortableTrackObjectAssetStatus portable_track_boundary_width(
    const PortableTrackObjectStream *stream,
    uint32_t *out_width);

#endif
