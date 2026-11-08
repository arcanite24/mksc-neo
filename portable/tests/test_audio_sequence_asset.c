#include "audio_sequence_asset.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "audio sequence asset failed line %d: %s\n", \
                __LINE__, #condition); \
        return 1; \
    } \
} while (0)

enum { EVENT_COUNT = 5, ASSET_SIZE = 32 + EVENT_COUNT * 16 };

static void put16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
    out[2] = (uint8_t)(value >> 16);
    out[3] = (uint8_t)(value >> 24);
}

static void event(uint8_t *asset, unsigned index, uint32_t tick,
                  uint32_t target, uint16_t value, uint8_t track, uint8_t op,
                  uint8_t arg0, uint8_t arg1)
{
    uint8_t *out = asset + 32u + index * 16u;
    put32(out, tick);
    put32(out + 4u, target);
    put16(out + 8u, value);
    out[10] = track;
    out[11] = op;
    out[12] = arg0;
    out[13] = arg1;
}

static void valid_asset(uint8_t asset[ASSET_SIZE])
{
    memset(asset, 0, ASSET_SIZE);
    memcpy(asset, "MKSCAUD1", 8u);
    put16(asset + 8u, 32u);
    put16(asset + 10u, 1u);
    put16(asset + 12u, 25u);
    asset[14] = 2u;
    asset[16] = 148u;
    put16(asset + 18u, 25u);
    put32(asset + 20u, EVENT_COUNT);
    put32(asset + 24u, 32u);
    put32(asset + 28u, ASSET_SIZE);
    event(asset, 0u, 0u, 0u, 66u, 0u, PORTABLE_AUDIO_EVENT_TEMPO, 0u, 0u);
    event(asset, 1u, 0u, 0u, 0u, 1u, PORTABLE_AUDIO_EVENT_MEMORY_SET, 0u, 1u);
    event(asset, 2u, 0u, 0u, 6u, 0u, PORTABLE_AUDIO_EVENT_NOTE, 67u, 100u);
    event(asset, 3u, 6u, 0u, 0u, 0u, PORTABLE_AUDIO_EVENT_LOOP, 0u, 0u);
    event(asset, 4u, 6u, 0u, 0u, 1u, PORTABLE_AUDIO_EVENT_END, 0u, 0u);
}

static PortableAudioSequenceStatus open_changed(size_t offset, uint8_t value)
{
    uint8_t asset[ASSET_SIZE];
    PortableAudioSequenceView view;
    valid_asset(asset);
    asset[offset] = value;
    return portable_audio_sequence_open(&view, asset, sizeof asset);
}

static int test_valid(void)
{
    uint8_t asset[ASSET_SIZE];
    PortableAudioSequenceView view;
    PortableAudioSequenceEvent decoded;
    valid_asset(asset);
    CHECK(portable_audio_sequence_open(&view, asset, sizeof asset) ==
          PORTABLE_AUDIO_SEQUENCE_OK);
    CHECK(view.song_id == 25u && view.track_count == 2u &&
          view.event_count == EVENT_COUNT);
    CHECK(portable_audio_sequence_event(&view, 2u, &decoded) ==
          PORTABLE_AUDIO_SEQUENCE_OK);
    CHECK(decoded.operation == PORTABLE_AUDIO_EVENT_NOTE &&
          decoded.value == 6u && decoded.arg0 == 67u && decoded.arg1 == 100u);
    CHECK(portable_audio_sequence_event(&view, EVENT_COUNT, &decoded) ==
          PORTABLE_AUDIO_SEQUENCE_BAD_SIZE);
    return 0;
}

static int test_header_rejections(void)
{
    uint8_t asset[ASSET_SIZE];
    PortableAudioSequenceView view;
    CHECK(open_changed(0u, 'X') == PORTABLE_AUDIO_SEQUENCE_BAD_MAGIC);
    CHECK(open_changed(10u, 3u) == PORTABLE_AUDIO_SEQUENCE_BAD_VERSION);
    CHECK(open_changed(14u, 0u) == PORTABLE_AUDIO_SEQUENCE_BAD_TRACK);
    valid_asset(asset);asset[10]=2u;asset[17]=10u;
    CHECK(portable_audio_sequence_open(&view,asset,sizeof asset)==PORTABLE_AUDIO_SEQUENCE_OK);
    CHECK(view.player_index==10u);
    asset[17]=11u;
    CHECK(portable_audio_sequence_open(&view,asset,sizeof asset)==PORTABLE_AUDIO_SEQUENCE_BAD_SIZE);
    valid_asset(asset);
    CHECK(portable_audio_sequence_open(&view,asset,sizeof asset)==PORTABLE_AUDIO_SEQUENCE_OK);
    CHECK(view.player_index==255u);
    valid_asset(asset);
    put16(asset + 12u, 403u);
    CHECK(portable_audio_sequence_open(&view, asset, sizeof asset) ==
          PORTABLE_AUDIO_SEQUENCE_BAD_SONG);
    valid_asset(asset);
    put32(asset + 24u, ASSET_SIZE + 1u);
    CHECK(portable_audio_sequence_open(&view, asset, sizeof asset) ==
          PORTABLE_AUDIO_SEQUENCE_BAD_SIZE);
    valid_asset(asset);
    put32(asset + 20u, EVENT_COUNT + 1u);
    CHECK(portable_audio_sequence_open(&view, asset, sizeof asset) ==
          PORTABLE_AUDIO_SEQUENCE_BAD_SIZE);
    return 0;
}

static int test_event_rejections(void)
{
    uint8_t asset[ASSET_SIZE];
    PortableAudioSequenceView view;
    valid_asset(asset);
    asset[32u + 11u] = 0xffu;
    CHECK(portable_audio_sequence_open(&view, asset, sizeof asset) ==
          PORTABLE_AUDIO_SEQUENCE_BAD_OPERATION);
    valid_asset(asset);
    asset[32u + 2u * 16u + 10u] = 2u;
    CHECK(portable_audio_sequence_open(&view, asset, sizeof asset) ==
          PORTABLE_AUDIO_SEQUENCE_BAD_TRACK);
    valid_asset(asset);
    put32(asset + 32u + 3u * 16u + 4u, 3u);
    CHECK(portable_audio_sequence_open(&view, asset, sizeof asset) ==
          PORTABLE_AUDIO_SEQUENCE_BAD_TARGET);
    valid_asset(asset);
    put32(asset + 32u + 3u * 16u + 4u, 1u);
    CHECK(portable_audio_sequence_open(&view, asset, sizeof asset) ==
          PORTABLE_AUDIO_SEQUENCE_BAD_TARGET);
    valid_asset(asset);
    put32(asset + 32u + 3u * 16u, 0u);
    CHECK(portable_audio_sequence_open(&view, asset, sizeof asset) ==
          PORTABLE_AUDIO_SEQUENCE_BAD_TARGET);
    valid_asset(asset);
    put32(asset + 32u + 2u * 16u, 7u);
    CHECK(portable_audio_sequence_open(&view, asset, sizeof asset) ==
          PORTABLE_AUDIO_SEQUENCE_BAD_ORDER);
    valid_asset(asset);
    asset[32u + 4u * 16u + 11u] = PORTABLE_AUDIO_EVENT_VOLUME;
    CHECK(portable_audio_sequence_open(&view, asset, sizeof asset) ==
          PORTABLE_AUDIO_SEQUENCE_UNTERMINATED_TRACK);
    valid_asset(asset);
    asset[32u + 2u * 16u + 14u] = 1u;
    CHECK(portable_audio_sequence_open(&view, asset, sizeof asset) ==
          PORTABLE_AUDIO_SEQUENCE_BAD_PARAMETER);
    return 0;
}

static int test_file(const char *path)
{
    PortableAudioSequenceView view;
    PortableAudioSequenceStatus status;
    uint8_t *bytes;
    FILE *file = fopen(path, "rb");
    long size;
    CHECK(file != NULL);
    CHECK(fseek(file, 0L, SEEK_END) == 0);
    size = ftell(file);
    CHECK(size > 0 && fseek(file, 0L, SEEK_SET) == 0);
    bytes = (uint8_t *)malloc((size_t)size);
    CHECK(bytes != NULL && fread(bytes, 1u, (size_t)size, file) == (size_t)size);
    CHECK(fclose(file) == 0);
    status = portable_audio_sequence_open(&view, bytes, (size_t)size);
    if (status != PORTABLE_AUDIO_SEQUENCE_OK)
        fprintf(stderr, "portable audio sequence file rejected: %s\n",
                portable_audio_sequence_status_name(status));
    CHECK(status == PORTABLE_AUDIO_SEQUENCE_OK);
    printf("portable audio sequence file: song=%u tracks=%u events=%u\n",
           view.song_id, view.track_count, view.event_count);
    free(bytes);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2)
        return test_file(argv[1]);
    CHECK(argc == 1);
    CHECK(test_valid() == 0);
    CHECK(test_header_rejections() == 0);
    CHECK(test_event_rejections() == 0);
    puts("portable audio sequence asset: PASS");
    return 0;
}
