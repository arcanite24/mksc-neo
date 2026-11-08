#include "n64_sequence.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok)
    {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

static void put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static void put32(uint8_t *p, uint32_t v)
{
    put16(p, v);
    put16(p + 2, v >> 16);
}
static void putf(uint8_t *p, float f)
{
    uint32_t bits;
    memcpy(&bits, &f, sizeof bits);
    put32(p, bits);
}
static void be16(uint8_t *p, int32_t v)
{
    p[0] = (uint8_t)((uint32_t)v >> 8);
    p[1] = (uint8_t)v;
}

/* One looping VADPCM square-wave instrument, one sequence. */
enum { SEQUENCE = 72, INSTRUMENT = 200, SAMPLE = 232, DATA = 260 };
enum { ENVELOPE = 0, BOOK = 16, STATE = 56, ADPCM = 88, FRAMES = 8, DATA_SIZE = ADPCM + FRAMES * 9 };
enum { SECTION = DATA + DATA_SIZE };

static const uint8_t song[] = {
    /* 0 */ 0xdd, 120,             /* tempo 120 */
    /* 2 */ 0xdb, 127,             /* full volume */
    /* 4 */ 0xd7, 0x00, 0x01,      /* init channel 0 */
    /* 7 */ 0x90, 0x00, 0x11,      /* start channel 0 at 0x11 */
    /* 10 */ 0xfe,                 /* wait a tatum */
    /* 11 */ 0x00,                 /* channel 0 finished? */
    /* 12 */ 0xfa, 0x00, 0x0a,     /* no: wait again */
    /* 15 */ 0xff, 0x00,
    /* 0x11 channel */ 0xc1, 0x00, /* instrument 0 */
    0xdf, 127, 0xdd, 64,           /* volume, centre pan */
    0x90, 0x00, 0x20,              /* layer 0 at 0x20 */
    /* 0x1a */ 0xfe, 0x00,         /* wait; layer 0 finished? */
    0xfa, 0x00, 0x1a,
    0xff,
    /* 0x20 layer */ 0xc1, 0x7f,   /* velocity 127 */
    0x27, 0x30,                    /* semitone 39 for 48 tatums */
    0xff,
};

static void build(uint8_t section[SECTION], const uint8_t *sequence, uint32_t sequence_size)
{
    memset(section, 0, SECTION);
    memcpy(section, "MKSCN64S", 8);
    put16(section + 8, 72);
    put16(section + 10, 1);
    section[12] = 3;
    section[13] = 0xff;
    section[16 + 1] = 2u << 4 | 1u; /* bank 1: channel 2 */
    section[64 + 1] = 5;
    put32(section + 24, SEQUENCE);
    put32(section + 28, sequence_size);
    put32(section + 32, INSTRUMENT);
    put32(section + 36, 1);
    put32(section + 40, 0);
    put32(section + 44, 0);
    put32(section + 48, SAMPLE);
    put32(section + 52, 1);
    put32(section + 56, DATA);
    put32(section + 60, DATA_SIZE);
    memcpy(section + SEQUENCE, sequence, sequence_size);
    uint8_t *inst = section + INSTRUMENT;
    inst[0] = 1;
    inst[1] = 0;
    inst[2] = 127;
    inst[3] = 0x10;
    put32(inst + 4, ENVELOPE);
    for (unsigned i = 0; i < 3u; ++i)
    {
        put16(inst + 8 + i * 8u, 0);
        putf(inst + 12 + i * 8u, 1.0f);
    }
    uint8_t *sample = section + SAMPLE;
    put32(sample, ADPCM);
    put32(sample + 4, FRAMES * 9);
    put32(sample + 8, BOOK);
    put32(sample + 12, 0);
    put32(sample + 16, FRAMES * 16);
    put32(sample + 20, 0xffffffffu);
    put32(sample + 24, STATE);
    uint8_t *data = section + DATA;
    be16(data + ENVELOPE, 2);
    be16(data + ENVELOPE + 2, 32000);
    be16(data + ENVELOPE + 4, -1);
    be16(data + BOOK + 2, 2); /* order 2 */
    be16(data + BOOK + 6, 1); /* one predictor, all zero */
    for (unsigned f = 0; f < FRAMES; ++f)
    {
        uint8_t *frame = data + ADPCM + f * 9u;
        frame[0] = 12u << 4; /* scale 4096, predictor 0 */
        memset(frame + 1, f & 1u ? 0x88 : 0x77, 8);
    }
}

static void test_vadpcm(void)
{
    uint8_t book[32] = {0};
    int16_t history[2] = {0, 0}, out[16];
    const uint8_t frame[9] = {0x10, 0x12, 0x34, 0x56, 0x7f, 0x89, 0xab, 0xcd, 0xef};
    portable_n64_vadpcm_frame(frame, book, 1, history, out);
    static const int16_t raw[16] = {1, 2, 3, 4, 5, 6, 7, -1, -8, -7, -6, -5, -4, -3, -2, -1};
    int ok = 1;
    for (unsigned i = 0; i < 16u; ++i)
        ok &= out[i] == raw[i] * 2;
    expect(ok, "zero book decodes nibble * scale");
    expect(history[0] == out[14] && history[1] == out[15], "history keeps the last two samples");
    /* Coefficient book[1][0] = 2048 (1.0): sample 0 adds the previous one;
     * book[1][1] = 2048 adds ix[0] into sample 1, and history[0] enters
     * through book[0][i]. */
    be16(book + 16, 2048);
    be16(book + 18, 2048);
    be16(book + 0, -1024);
    int16_t h[2] = {100, 200}, o[16];
    const uint8_t one[9] = {0x00, 0x10, 0, 0, 0, 0, 0, 0, 0};
    portable_n64_vadpcm_frame(one, book, 1, h, o);
    expect(o[0] == 200 - 50 + 1, "first sample uses both history samples");
    expect(o[1] == (2048 * 200 + 2048 * 1) / 2048, "second sample folds ix[0] through book[order-1]");
    const uint8_t clip[9] = {0xf0, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77};
    int16_t z[2] = {0, 0};
    portable_n64_vadpcm_frame(clip, (const uint8_t[32]){0}, 1, z, o);
    expect(o[0] == 32767, "decoded samples clamp to s16");
}

static void test_open_bounds(void)
{
    static uint8_t section[SECTION];
    PortableN64SequenceView view;
    build(section, song, sizeof song);
    expect(portable_n64_sequence_open(&view, section, sizeof section) == PORTABLE_N64_OK, "valid section opens");
    expect(view.player == 3u && view.variation == -1 && view.effect_channels[1] == 0x21u && view.bank_players[1] == 5u, "header fields");
    expect(portable_n64_sequence_open(&view, section, 71) == PORTABLE_N64_BAD_SECTION, "short header rejected");
    section[0] = 'X';
    expect(portable_n64_sequence_open(&view, section, sizeof section) == PORTABLE_N64_BAD_SECTION, "magic checked");
    build(section, song, sizeof song);
    put32(section + 28, SECTION);
    expect(portable_n64_sequence_open(&view, section, sizeof section) == PORTABLE_N64_BAD_SECTION,
           "sequence past the section rejected");
    build(section, song, sizeof song);
    put32(section + SAMPLE + 4, FRAMES * 9 + 9);
    expect(portable_n64_sequence_open(&view, section, sizeof section) == PORTABLE_N64_BAD_SECTION,
           "ADPCM past the data rejected");
    build(section, song, sizeof song);
    put32(section + SAMPLE + 16, FRAMES * 16 + 1);
    expect(portable_n64_sequence_open(&view, section, sizeof section) == PORTABLE_N64_BAD_SECTION,
           "loop end past the frames rejected");
    build(section, song, sizeof song);
    be16(section + DATA + BOOK + 2, 3);
    expect(portable_n64_sequence_open(&view, section, sizeof section) == PORTABLE_N64_BAD_SECTION,
           "only order-2 books");
    build(section, song, sizeof song);
    put16(section + INSTRUMENT + 8, 1);
    expect(portable_n64_sequence_open(&view, section, sizeof section) == PORTABLE_N64_BAD_SECTION,
           "instrument sample index checked");
    build(section, song, sizeof song);
    be16(section + DATA + ENVELOPE + 4, -4); /* no such envelope command */
    expect(portable_n64_sequence_open(&view, section, sizeof section) == PORTABLE_N64_BAD_SECTION,
           "invalid envelope command rejected");
}

static PortableN64Player player;

static void test_play(void)
{
    static uint8_t section[SECTION];
    PortableN64SequenceView view;
    build(section, song, sizeof song);
    expect(portable_n64_sequence_open(&view, section, sizeof section) == PORTABLE_N64_OK, "song opens");
    portable_n64_player_start(&player, &view);
    int peak = 0, ticks = 0;
    int16_t frame[PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 2u];
    while (portable_n64_player_playing(&player) && ticks < 2000)
    {
        memset(frame, 0, sizeof frame);
        expect(portable_n64_player_mix(&player, frame) == PORTABLE_N64_OK, "mix ok");
        for (unsigned i = 0; i < PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 2u; ++i)
            peak = abs(frame[i]) > peak ? abs(frame[i]) : peak;
        ++ticks;
    }
    /* 48 tatums at 120 bpm (96 tatums/s) is half a second, plus release. */
    expect(peak > 4000, "the note sounds");
    expect(ticks > 25 && ticks < 120, "the song ends after its note");

    /* Tempo scale doubles the tatum rate. */
    portable_n64_player_start(&player, &view);
    player.tempo_scale = 512;
    int fast = 0;
    while (portable_n64_player_playing(&player) && fast < 2000)
    {
        memset(frame, 0, sizeof frame);
        portable_n64_player_mix(&player, frame);
        ++fast;
    }
    expect(fast < ticks, "tempo scale speeds the song up");

    /* A jump outside the sequence fails loudly. */
    uint8_t bad[sizeof song];
    memcpy(bad, song, sizeof song);
    bad[13] = 0x7f; /* wait loop jumps to 0x7f0a */
    build(section, bad, sizeof bad);
    expect(portable_n64_sequence_open(&view, section, sizeof section) == PORTABLE_N64_OK, "bad song opens");
    portable_n64_player_start(&player, &view);
    PortableN64Status status = PORTABLE_N64_OK;
    for (int i = 0; i < 20 && status == PORTABLE_N64_OK; ++i)
    {
        memset(frame, 0, sizeof frame);
        status = portable_n64_player_mix(&player, frame);
    }
    expect(status == PORTABLE_N64_BAD_ADDRESS && player.failed_at == 0x7f0au && !portable_n64_player_playing(&player),
           "out-of-range jump names its address and stops");
}

/* A sound-effect channel waits for IO port 0, plays dyntable[port 4] and
 * reports done on port 7. */
static const uint8_t effects[] = {
    /* 0 */ 0xd7, 0x00, 0x04,       /* init channel 2 */
    /* 3 */ 0x92, 0x00, 0x08,       /* start channel 2 at 8 */
    /* 6 */ 0xfd, 0x7f,             /* idle */
    /* 8 channel */ 0xc1, 0x00, 0xc2, 0x00, 0x24, /* instrument 0; dyntable at 0x24 */
    /* 0x0d */ 0xfe, 0x80,          /* wait; read port 0 */
    0xc8, 0x01, 0xfa, 0x00, 0x17,   /* == 1: start */
    0xfb, 0x00, 0x0d,
    /* 0x17 */ 0x84, 0xe4,          /* read port 4; dyncall */
    /* 0x19 */ 0xfe, 0x00, 0xfa, 0x00, 0x19, /* wait for layer 0 */
    0xcc, 0x80, 0x77,               /* port 7 = 0x80 */
    0xfb, 0x00, 0x0d,
    /* 0x24 dyntable */ 0x00, 0x28, 0x00, 0x28,
    /* 0x28 sound */ 0x90, 0x00, 0x2c, 0xff,
    /* 0x2c layer */ 0xc1, 0x7f, 0x27, 0x10, 0xff,
};

static void test_effect(void)
{
    static uint8_t section[SECTION];
    PortableN64SequenceView view;
    build(section, effects, sizeof effects);
    expect(portable_n64_sequence_open(&view, section, sizeof section) == PORTABLE_N64_OK, "effects open");
    expect(portable_n64_player_start_effect(&player, &view, 0, 1) == PORTABLE_N64_INVALID_ARGUMENT,
           "a bank without channels is rejected");
    expect(portable_n64_player_start_effect(&player, &view, 1, 1) == PORTABLE_N64_OK, "effect starts");
    int peak = 0, ticks = 0;
    int16_t frame[PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 2u];
    while (portable_n64_player_playing(&player) && ticks < 2000)
    {
        memset(frame, 0, sizeof frame);
        expect(portable_n64_player_mix(&player, frame) == PORTABLE_N64_OK, "effect mix ok");
        for (unsigned i = 0; i < PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 2u; ++i)
            peak = abs(frame[i]) > peak ? abs(frame[i]) : peak;
        ++ticks;
    }
    expect(peak > 4000, "the requested sound plays");
    expect(ticks > 5 && ticks < 100, "the effect ends when its channel reports done");
}

int main(void)
{
    test_vadpcm();
    test_open_bounds();
    test_play();
    test_effect();
    if (failures)
        return 1;
    puts("n64 sequence tests passed");
    return 0;
}
