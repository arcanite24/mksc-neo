#ifndef PORTABLE_SOURCE_COURSE_METADATA_H
#define PORTABLE_SOURCE_COURSE_METADATA_H

#include <stddef.h>
#include <stdint.h>

#include "track_asset.h"

typedef enum
{
    PORTABLE_SOURCE_COURSE_OK = 0,
    PORTABLE_SOURCE_COURSE_INVALID_ARGUMENT,
    PORTABLE_SOURCE_COURSE_BAD_TRACK_TABLE,
    PORTABLE_SOURCE_COURSE_BAD_TRACK_DEFINITION,
    PORTABLE_SOURCE_COURSE_BAD_TRACK_BLOCK,
    PORTABLE_SOURCE_COURSE_BAD_BOUNDARY_STREAM,
    PORTABLE_SOURCE_COURSE_BAD_START_FINISH,
} PortableSourceCourseStatus;

typedef struct
{
    uint32_t header_index;
    uint32_t course_id;
    uint32_t background_id;
    uint16_t checkpoint_count;
    uint16_t start_route_index;
    int32_t start_x_q16;
    int32_t start_y_q16;
    int32_t finish_y_q16;
} PortableSourceCourseMetadata;

PortableSourceCourseStatus portable_source_course_selection(
    const uint8_t *rom,
    size_t rom_size,
    uint32_t header_index,
    uint32_t *out_course_id,
    uint32_t *out_background_id);

PortableSourceCourseStatus portable_source_course_metadata_build(
    const uint8_t *rom,
    size_t rom_size,
    uint32_t header_index,
    const PortableTrackAsset *asset,
    PortableSourceCourseMetadata *out);

const char *portable_source_course_status_name(
    PortableSourceCourseStatus status);

#endif
