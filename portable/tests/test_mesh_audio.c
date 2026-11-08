#include "game_host_internal.h"

#define CHECK(condition)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(condition))                                                                          \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);                         \
            result = 1;                                                                            \
            goto done;                                                                             \
        }                                                                                          \
    } while (0)

static uint8_t *read_pack(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    if (!file)
        return NULL;
    uint8_t *bytes = NULL;
    if (fseek(file, 0L, SEEK_END) != 0)
        goto done;
    const long length = ftell(file);
    if (length <= 0 || length > 64L * 1024L * 1024L || fseek(file, 0L, SEEK_SET) != 0)
        goto done;
    bytes = malloc((size_t)length);
    if (!bytes || fread(bytes, 1u, (size_t)length, file) != (size_t)length)
    {
        free(bytes);
        bytes = NULL;
    }
    else
        *size = (size_t)length;
done:
    if (fclose(file) != 0)
    {
        free(bytes);
        bytes = NULL;
    }
    return bytes;
}

int main(int argc, char **argv)
{
    int result = 0;
    uint8_t *bytes = NULL;
    RouteAudioHost *audio = calloc(1u, sizeof *audio);
    PortableAudioSession *snapshot = malloc(sizeof *snapshot);
    CHECK(audio && snapshot && argc == 2);
    size_t size = 0u;
    bytes = read_pack(argv[1], &size);
    PortableAssetPackView pack;
    CHECK(bytes && portable_asset_pack_open(bytes, size, &pack) == PORTABLE_ASSET_PACK_OK);
    CHECK(SDL_Init(SDL_INIT_AUDIO));
    CHECK(portable_sdl_audio_queue_open(&audio->queue) == PORTABLE_SDL_AUDIO_OK);
    SDL_AudioStream *stream = audio->queue.stream;
    const SDL_AudioDeviceID device = audio->queue.device;
    audio->capture_attempted = 1; /* This unit does not consume a caller's capture path. */
    PortableMeshAudioConfig config = {.pack = &pack, .course_id = 70000u, .driver_id = 90000u,
        .music_kind = UINT32_MAX, .music_id = UINT32_MAX, .mode = 2u,
        .voice_class = 0u, .engine_class = 0u};
    CHECK(route_audio_mesh_begin(audio, &config));
    CHECK(audio->session.pack == &pack && audio->queue.stream == stream &&
          audio->queue.device == device && audio->capture_attempted == 1);
    CHECK(audio->race_timeline.course_song == 0 && !audio->session.players[0].active);
    CHECK(audio->session.pcm.tuning == &audio->session.tuning &&
          audio->session.psg.tuning == &audio->session.tuning);
    PortableMeshAudioFrame frame = {.phase = PORTABLE_MESH_AUDIO_COUNTDOWN,
        .countdown_remaining = 120, .target_speed_q16 = 20 * 65536, .lap_count = 3,
        .racer_count = 1};
    CHECK(route_audio_mesh_tick(audio, &frame));
    CHECK(audio->session.rendered_frames == 1 && audio->session.players[7].song_id == 114);
    memcpy(snapshot, &audio->session, sizeof *snapshot);
    const uint32_t sequence = audio->command_sequence;
    const uint32_t submitted = audio->queue.submitted_blocks;
    CHECK(route_audio_mesh_tick(audio, &frame));
    CHECK(memcmp(snapshot, &audio->session, sizeof *snapshot) == 0 &&
          audio->command_sequence == sequence && audio->queue.submitted_blocks == submitted);
    frame.tick = 1;
    frame.countdown_remaining = 60;
    CHECK(route_audio_mesh_tick(audio, &frame));
    CHECK(audio->session.players[7].song_id == 114);
    frame.tick = 2;
    frame.countdown_remaining = 0;
    frame.phase = PORTABLE_MESH_AUDIO_RACING;
    CHECK(route_audio_mesh_tick(audio, &frame));
    CHECK(audio->session.players[7].song_id == 115 && !audio->session.players[0].active);
    frame.tick = 3;
    frame.speed_q16 = 5 * 65536;
    frame.buttons = 1;
    frame.hopped = 1;
    CHECK(route_audio_mesh_tick(audio, &frame));
    CHECK(portable_audio_session_song_loaded(&audio->session, 157));
    CHECK(audio->mesh_audio.engine_song == audio->engine_rules.throttle_songs[0]);
    frame.tick = 4;
    frame.hopped = 0;
    frame.boost_released = 1;
    CHECK(route_audio_mesh_tick(audio, &frame));
    CHECK(portable_audio_session_song_loaded(&audio->session, 140));
    frame.tick = 5;
    frame.boost_released = 0;
    frame.respawning = 1;
    CHECK(route_audio_mesh_tick(audio, &frame));
    CHECK(portable_audio_session_song_loaded(&audio->session, 161) &&
          audio->mesh_audio.engine_song == 0);
    const uint32_t mixed = audio->session.rendered_frames;
    frame.paused = 1;
    CHECK(route_audio_mesh_tick(audio, &frame));
    CHECK(audio->session.rendered_frames == mixed && audio->race_paused);
    frame.tick = 6;
    CHECK(route_audio_mesh_tick(audio, &frame));
    CHECK(audio->session.rendered_frames == mixed);
    frame.paused = 0;
    CHECK(route_audio_mesh_tick(audio, &frame));
    CHECK(audio->session.rendered_frames == mixed + 1u && !audio->race_paused);
    frame.tick = 7;
    frame.respawning = 0;
    frame.landed = 1;
    frame.buttons = 0;
    CHECK(route_audio_mesh_tick(audio, &frame));
    CHECK(audio->mesh_audio.engine_song == audio->engine_rules.coast_songs[0] &&
          portable_audio_session_song_loaded(&audio->session, 159));
    frame.tick = 8;
    frame.landed = 0;
    frame.laps_completed = 1;
    CHECK(route_audio_mesh_tick(audio, &frame));
    CHECK(portable_audio_session_song_loaded(&audio->session, audio->finish_rules.lap_song));
    frame.tick = 9;
    frame.laps_completed = 2;
    CHECK(route_audio_mesh_tick(audio, &frame));
    CHECK(audio->session.players[0].song_id == 19 && audio->lap_audio.countdown > 0);
    const uint16_t lap_wait = audio->lap_audio.countdown;
    frame.paused = 1;
    CHECK(route_audio_mesh_tick(audio, &frame));
    CHECK(audio->lap_audio.countdown == lap_wait);
    frame.paused = 0;
    while (audio->lap_audio.countdown)
    {
        ++frame.tick;
        CHECK(route_audio_mesh_tick(audio, &frame));
    }
    CHECK(!audio->session.players[0].active); /* No authored background music. */
    ++frame.tick;
    frame.finished = 1;
    frame.phase = PORTABLE_MESH_AUDIO_FINISHED;
    frame.laps_completed = 3;
    CHECK(route_audio_mesh_tick(audio, &frame));
    CHECK(audio->finish_audio.started && audio->mesh_audio.engine_song == 0);
    const uint16_t delay = audio->finish_audio.countdown;
    CHECK(delay > 0u && delay < 4096u);
    memcpy(snapshot, &audio->session, sizeof *snapshot);
    CHECK(route_audio_mesh_tick(audio, &frame));
    CHECK(audio->finish_audio.countdown == delay &&
          memcmp(snapshot, &audio->session, sizeof *snapshot) == 0);
    for (unsigned i = 0; i < delay; ++i)
    {
        ++frame.tick;
        CHECK(route_audio_mesh_tick(audio, &frame));
    }
    CHECK(audio->finish_audio.countdown == 0u &&
          portable_audio_session_song_loaded(&audio->session, audio->finish_audio.music));

    /* Failed mixer/profile or full-width META references do not retire the
     * active generation, queue, capture owner, or current PCM voices. */
    memcpy(snapshot, &audio->session, sizeof *snapshot);
    PortableAssetPackView absent = {0};
    CHECK(!route_audio_rebind_pack(audio, &absent));
    CHECK(memcmp(snapshot, &audio->session, sizeof *snapshot) == 0);
    config.music_kind = PORTABLE_ASSET_SECTION_AUDIO_SEQUENCE;
    config.music_id = 65536u;
    CHECK(!route_audio_mesh_begin(audio, &config));
    CHECK(memcmp(snapshot, &audio->session, sizeof *snapshot) == 0 &&
          audio->queue.stream == stream && audio->queue.device == device);

    /* A valid META song starts at the countdown owner's GO+80 boundary;
     * absent META above never substitutes an unrelated stock course song. */
    config.music_id = 2u;
    CHECK(route_audio_mesh_begin(audio, &config));
    frame = (PortableMeshAudioFrame){.phase = PORTABLE_MESH_AUDIO_RACING,
        .target_speed_q16 = 20 * 65536, .lap_count = 3, .racer_count = 1};
    CHECK(route_audio_mesh_tick(audio, &frame));
    CHECK(!audio->session.players[0].active);
    for (unsigned i = 1; i <= 80; ++i)
    {
        frame.tick = i;
        CHECK(route_audio_mesh_tick(audio, &frame));
        if (i < 80)
            CHECK(!audio->session.players[0].active);
    }
    CHECK(audio->session.players[0].song_id == 2u && audio->session.players[0].active);
    frame.tick = 79;
    const uint32_t retained_frames = audio->session.rendered_frames;
    CHECK(!route_audio_mesh_tick(audio, &frame));
    CHECK(audio->session.rendered_frames == retained_frames);
    /* Primary Battle Bomb role stops the existing engine once and never
     * restarts normal kart audio on continued accepted or repeated ticks. */
    config.mode=16u;config.music_kind=config.music_id=UINT32_MAX;
    CHECK(route_audio_mesh_begin(audio,&config));
    frame=(PortableMeshAudioFrame){.phase=PORTABLE_MESH_AUDIO_RACING,
        .target_speed_q16=20*65536,.speed_q16=5*65536,.buttons=1,.racer_count=4};
    CHECK(route_audio_mesh_tick(audio,&frame));
    const uint8_t engine_player=audio->mesh_audio.engine_player;
    CHECK(engine_player<PORTABLE_AUDIO_PLAYER_COUNT &&
          audio->mesh_audio.engine_song==audio->engine_rules.throttle_songs[0]);
    frame.tick=1;frame.revenge_bomb=1;
    CHECK(route_audio_mesh_tick(audio,&frame));
    CHECK(audio->mesh_audio.engine_song==0 && audio->mesh_audio.engine_player==UINT8_MAX &&
          !audio->session.players[engine_player].active);
    const uint32_t bomb_commands=audio->command_sequence;
    memcpy(snapshot,&audio->session,sizeof *snapshot);
    CHECK(route_audio_mesh_tick(audio,&frame));
    CHECK(audio->command_sequence==bomb_commands &&
          !memcmp(snapshot,&audio->session,sizeof *snapshot));
    for(unsigned tick=2;tick<6;++tick)
    {
        frame.tick=tick;CHECK(route_audio_mesh_tick(audio,&frame));
        CHECK(audio->mesh_audio.engine_song==0 && audio->mesh_audio.engine_player==UINT8_MAX &&
              audio->command_sequence==bomb_commands);
    }
    frame.tick=6;frame.revenge_bomb=0;
    CHECK(route_audio_mesh_tick(audio,&frame));
    CHECK(audio->mesh_audio.engine_song==audio->engine_rules.throttle_songs[0]);
    CHECK(route_audio_rebind_pack(audio, &pack) && !audio->mesh_audio.active);
    CHECK(route_audio_return_menu(audio) && audio->session.players[0].song_id == 2u);
    CHECK(audio->queue.stream == stream && audio->queue.device == device &&
          audio->capture_attempted == 1);
done:
    if (audio)
    {
        portable_sdl_audio_queue_close(&audio->queue);
        free(audio);
    }
    free(snapshot);
    free(bytes);
    SDL_Quit();
    return result;
}
