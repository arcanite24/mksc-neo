#include "audio_command_stream.h"

#include <stdio.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "audio command stream failed line %d: %s\n", \
                __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static int test_order_and_checksum(void)
{
    PortableAudioCommand storage[5];
    PortableAudioCommandStream stream;
    CHECK(portable_audio_command_stream_init(&stream, storage, 5u) ==
          PORTABLE_AUDIO_STREAM_OK);
    CHECK(portable_audio_command_stream_emit(&stream, 92u,
          PORTABLE_AUDIO_COMMAND_START_SONG, PORTABLE_AUDIO_PLAYER_AUTO, 53u) ==
          PORTABLE_AUDIO_STREAM_OK);
    CHECK(portable_audio_command_stream_emit(&stream, 734u,
          PORTABLE_AUDIO_COMMAND_START_SONG, PORTABLE_AUDIO_PLAYER_AUTO, 52u) ==
          PORTABLE_AUDIO_STREAM_OK);
    CHECK(portable_audio_command_stream_emit(&stream, 1001u,
          PORTABLE_AUDIO_COMMAND_START_SONG, PORTABLE_AUDIO_PLAYER_AUTO, 143u) ==
          PORTABLE_AUDIO_STREAM_OK);
    CHECK(portable_audio_command_stream_emit(&stream, 1018u,
          PORTABLE_AUDIO_COMMAND_FADE_PLAYER, 0u, 4u) ==
          PORTABLE_AUDIO_STREAM_OK);
    CHECK(portable_audio_command_stream_emit(&stream, 1022u,
          PORTABLE_AUDIO_COMMAND_START_SONG, PORTABLE_AUDIO_PLAYER_AUTO, 2u) ==
          PORTABLE_AUDIO_STREAM_OK);
    CHECK(stream.count == 5u && storage[4].sequence == 4u);
    CHECK(portable_audio_command_stream_checksum(&stream) ==
          UINT32_C(0x6ff6cffd));
    return 0;
}

static int test_equal_tick_order(void)
{
    PortableAudioCommand storage[3];
    PortableAudioCommandStream stream;
    CHECK(portable_audio_command_stream_init(&stream, storage, 3u) ==
          PORTABLE_AUDIO_STREAM_OK);
    CHECK(portable_audio_command_stream_emit(&stream, 61u,
          PORTABLE_AUDIO_COMMAND_PAUSE_PLAYER, 0u, 0u) ==
          PORTABLE_AUDIO_STREAM_OK);
    CHECK(portable_audio_command_stream_emit(&stream, 61u,
          PORTABLE_AUDIO_COMMAND_PAUSE_PLAYER, 1u, 0u) ==
          PORTABLE_AUDIO_STREAM_OK);
    CHECK(portable_audio_command_stream_emit(&stream, 62u,
          PORTABLE_AUDIO_COMMAND_START_SONG, 9u, 142u) ==
          PORTABLE_AUDIO_STREAM_OK);
    CHECK(storage[0].sequence == 0u && storage[1].sequence == 1u);
    return 0;
}

static int test_controls(void)
{
    PortableAudioCommand storage[3];
    PortableAudioCommandStream stream;
    CHECK(portable_audio_command_stream_init(&stream,storage,3u)==
          PORTABLE_AUDIO_STREAM_OK);
    CHECK(portable_audio_command_stream_emit(&stream,1u,
          PORTABLE_AUDIO_COMMAND_SET_VOLUME,1u,0x100u)==
          PORTABLE_AUDIO_STREAM_OK);
    CHECK(portable_audio_command_stream_emit(&stream,1u,
          PORTABLE_AUDIO_COMMAND_SET_PITCH,1u,(uint16_t)(int16_t)-128)==
          PORTABLE_AUDIO_STREAM_OK);
    CHECK(portable_audio_command_stream_emit(&stream,1u,
          PORTABLE_AUDIO_COMMAND_SET_TEMPO,0u,320u)==PORTABLE_AUDIO_STREAM_OK);
    return 0;
}

static int test_rejections_and_sticky_full(void)
{
    PortableAudioCommand storage[1];
    PortableAudioCommandStream stream;
    CHECK(portable_audio_command_stream_init(NULL, storage, 1u) ==
          PORTABLE_AUDIO_STREAM_INVALID_ARGUMENT);
    CHECK(portable_audio_command_stream_init(&stream, NULL, 1u) ==
          PORTABLE_AUDIO_STREAM_INVALID_ARGUMENT);
    CHECK(portable_audio_command_stream_init(&stream, storage, 1u) ==
          PORTABLE_AUDIO_STREAM_OK);
    CHECK(portable_audio_command_stream_emit(&stream, 0u,
          (PortableAudioCommandOperation)0, 0u, 0u) ==
          PORTABLE_AUDIO_STREAM_BAD_OPERATION);
    CHECK(portable_audio_command_stream_emit(&stream, 0u,
          PORTABLE_AUDIO_COMMAND_START_SONG, 11u, 2u) ==
          PORTABLE_AUDIO_STREAM_BAD_PLAYER);
    CHECK(portable_audio_command_stream_emit(&stream, 0u,
          PORTABLE_AUDIO_COMMAND_START_SONG, 9u, 142u) ==
          PORTABLE_AUDIO_STREAM_OK);
    CHECK(portable_audio_command_stream_init(&stream, storage, 1u) ==
          PORTABLE_AUDIO_STREAM_OK);
    CHECK(portable_audio_command_stream_emit(&stream, 0u,
          PORTABLE_AUDIO_COMMAND_START_SONG, PORTABLE_AUDIO_PLAYER_AUTO, 403u) ==
          PORTABLE_AUDIO_STREAM_BAD_SONG);
    CHECK(portable_audio_command_stream_emit(&stream, 0u,
          PORTABLE_AUDIO_COMMAND_STOP_PLAYER, 11u, 0u) ==
          PORTABLE_AUDIO_STREAM_BAD_PLAYER);
    CHECK(portable_audio_command_stream_emit(&stream, 0u,
          PORTABLE_AUDIO_COMMAND_PAUSE_PLAYER, 0u, 1u) ==
          PORTABLE_AUDIO_STREAM_BAD_PARAMETER);
    CHECK(portable_audio_command_stream_emit(&stream, 0u,
          PORTABLE_AUDIO_COMMAND_FADE_PLAYER, 0u, 0u) ==
          PORTABLE_AUDIO_STREAM_BAD_PARAMETER);
    CHECK(portable_audio_command_stream_emit(&stream, 0u,
          PORTABLE_AUDIO_COMMAND_SET_VOLUME, 0u, 0x400u) ==
          PORTABLE_AUDIO_STREAM_BAD_PARAMETER);
    CHECK(stream.count == 0u);
    CHECK(portable_audio_command_stream_emit(&stream, 10u,
          PORTABLE_AUDIO_COMMAND_START_SONG, PORTABLE_AUDIO_PLAYER_AUTO, 2u) ==
          PORTABLE_AUDIO_STREAM_OK);
    CHECK(portable_audio_command_stream_emit(&stream, 9u,
          PORTABLE_AUDIO_COMMAND_START_SONG, PORTABLE_AUDIO_PLAYER_AUTO, 3u) ==
          PORTABLE_AUDIO_STREAM_TICK_REGRESSION);
    CHECK(portable_audio_command_stream_emit(&stream, 11u,
          PORTABLE_AUDIO_COMMAND_START_SONG, PORTABLE_AUDIO_PLAYER_AUTO, 3u) ==
          PORTABLE_AUDIO_STREAM_FULL);
    CHECK(portable_audio_command_stream_emit(&stream, 12u,
          PORTABLE_AUDIO_COMMAND_START_SONG, PORTABLE_AUDIO_PLAYER_AUTO, 4u) ==
          PORTABLE_AUDIO_STREAM_FULL);
    CHECK(stream.count == 1u);
    return 0;
}

int main(void)
{
    CHECK(test_order_and_checksum() == 0);
    CHECK(test_equal_tick_order() == 0);
    CHECK(test_controls() == 0);
    CHECK(test_rejections_and_sticky_full() == 0);
    puts("portable audio command stream: PASS");
    return 0;
}
