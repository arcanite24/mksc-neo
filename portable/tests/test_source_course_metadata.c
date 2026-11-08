#include "source_course_metadata.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                       \
    do                                                                         \
    {                                                                          \
        if (!(condition))                                                      \
        {                                                                      \
            fprintf(stderr, "source course metadata failed line %d: %s\n",    \
                    __LINE__, #condition);                                     \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static void write_le16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void write_le32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void build_fixture(uint8_t *rom, uint8_t *blob)
{
    memset(rom, 0, 0xe8100u);
    memset(blob, 0, 0x400u);
    write_le32(rom + 0xe7fecu + 4u * 4u, 0x080e8000u);
    write_le32(rom + 0xe8000u, 24u);
    write_le32(rom + 0xe8004u, 6u);
    write_le32(blob + 0xccu, 0x100u);
    blob[0x100u] = 47u;
    write_le16(blob + 0x103u, 0x20u);
    write_le32(blob + 0x98u, 0x320u);
    blob[0x320u] = 0x8bu;
    blob[0x321u] = 106u;
    blob[0x322u] = 73u;
    blob[0x323u] = 1u;
}

int main(void)
{
    uint8_t rom[0xe8100u];
    uint8_t blob[0x400u];
    PortableTrackAsset asset = {blob, sizeof blob, 4u, 24u, 0u, sizeof blob};
    PortableSourceCourseMetadata metadata;
    uint32_t course_id = 0;
    uint32_t background_id = 0;

    build_fixture(rom, blob);
    CHECK(portable_source_course_selection(
              rom, sizeof rom, 4u, &course_id, &background_id) ==
          PORTABLE_SOURCE_COURSE_OK);
    CHECK(course_id == 24u && background_id == 6u);
    CHECK(portable_source_course_metadata_build(
              rom, sizeof rom, 4u, &asset, &metadata) ==
          PORTABLE_SOURCE_COURSE_OK);
    CHECK(metadata.course_id == 24u && metadata.background_id == 6u &&
          metadata.checkpoint_count == 47u &&
          metadata.start_route_index == 46u &&
          metadata.start_x_q16 == 888 * 65536 &&
          metadata.start_y_q16 == 612 * 65536 &&
          metadata.finish_y_q16 == 592 * 65536);

    blob[0x320u] = 0x8au;
    CHECK(portable_source_course_metadata_build(
              rom, sizeof rom, 4u, &asset, &metadata) ==
          PORTABLE_SOURCE_COURSE_BAD_START_FINISH);

    build_fixture(rom, blob);
    blob[0x324u] = 0x8bu;
    blob[0x325u] = 1u;
    blob[0x326u] = 1u;
    CHECK(portable_source_course_metadata_build(
              rom, sizeof rom, 4u, &asset, &metadata) ==
          PORTABLE_SOURCE_COURSE_BAD_START_FINISH);

    build_fixture(rom, blob);
    write_le32(blob + 0x98u, sizeof blob - 2u);
    CHECK(portable_source_course_metadata_build(
              rom, sizeof rom, 4u, &asset, &metadata) ==
          PORTABLE_SOURCE_COURSE_BAD_BOUNDARY_STREAM);

    build_fixture(rom, blob);
    blob[0x100u] = 1u;
    CHECK(portable_source_course_metadata_build(
              rom, sizeof rom, 4u, &asset, &metadata) ==
          PORTABLE_SOURCE_COURSE_BAD_TRACK_BLOCK);

    build_fixture(rom, blob);
    write_le16(blob + 0x103u, 0x300u);
    CHECK(portable_source_course_metadata_build(
              rom, sizeof rom, 4u, &asset, &metadata) ==
          PORTABLE_SOURCE_COURSE_BAD_TRACK_BLOCK);

    build_fixture(rom, blob);
    asset.course_index = 25u;
    CHECK(portable_source_course_metadata_build(
              rom, sizeof rom, 4u, &asset, &metadata) ==
          PORTABLE_SOURCE_COURSE_BAD_TRACK_DEFINITION);
    CHECK(portable_source_course_selection(
              rom, sizeof rom, 56u, &course_id, &background_id) ==
          PORTABLE_SOURCE_COURSE_BAD_TRACK_TABLE);

    puts("portable source course metadata: PASS");
    return 0;
}
