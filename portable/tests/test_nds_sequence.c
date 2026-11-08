#include "nds_sequence.h"

#include <stdio.h>
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

/* One program with one looping PCM8 square-wave region. */
enum { SEQUENCE = 48, PROGRAM = 80, REGION = 84, WAVE = 96, DATA = 120, SECTION = 184 };

static const uint8_t song[] = {
    0x81, 0x00,             /* program 0 */
    0xe1, 0x78, 0x00,       /* tempo 120 */
    0x3c, 0x7f, 0x30,       /* C4, velocity 127, 48 ticks */
    0x80, 0x30,             /* rest 48 ticks */
    0xff,
};

static void build(uint8_t section[SECTION], const uint8_t *sequence, uint32_t sequence_size)
{
    memset(section, 0, SECTION);
    memcpy(section, "MKSCNSQ1", 8);
    put16(section + 8, PORTABLE_NDS_SEQUENCE_HEADER_SIZE);
    put16(section + 10, 1);
    section[12] = 127;
    put32(section + 16, SEQUENCE);
    put32(section + 20, sequence_size);
    put32(section + 24, PROGRAM);
    put32(section + 28, 1);
    put32(section + 32, REGION);
    put32(section + 36, 1);
    put32(section + 40, WAVE);
    put32(section + 44, 1);
    memcpy(section + SEQUENCE, sequence, sequence_size);
    put16(section + PROGRAM + 2, 1);
    const uint8_t region[12] = {0, 127, 1, 60, 127, 127, 127, 100, 64, 0, 0, 0};
    memcpy(section + REGION, region, sizeof region);
    uint8_t *w = section + WAVE;
    w[1] = 1;
    put16(w + 4, 65536u - 1520u);
    put32(w + 12, 64);
    put32(w + 16, DATA);
    put32(w + 20, 64);
    for (unsigned i = 0; i < 64; ++i)
        section[DATA + i] = i < 32 ? 0x60 : 0xa0;
}

/* Plays up to `limit` ticks; returns the ticks played until the song ended. */
static uint32_t play(const PortableNdsSequenceView *view, uint32_t limit, uint64_t *energy, uint32_t *hash,
                     PortableNdsStatus *status)
{
    static PortableNdsPlayer player;
    portable_nds_player_start(&player, view);
    *energy = 0;
    *hash = 2166136261u;
    *status = PORTABLE_NDS_OK;
    uint32_t tick = 0;
    for (; tick < limit && portable_nds_player_playing(&player); ++tick)
    {
        int16_t frame[PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 2u] = {0};
        *status = portable_nds_player_mix(&player, frame);
        if (*status != PORTABLE_NDS_OK)
            break;
        for (unsigned i = 0; i < PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 2u; ++i)
        {
            *energy += (uint64_t)((int32_t)frame[i] * frame[i]);
            *hash = (*hash ^ (uint16_t)frame[i]) * 16777619u;
        }
    }
    return tick;
}

int main(void)
{
    uint8_t section[SECTION];
    PortableNdsSequenceView view;
    PortableNdsStatus status;
    uint64_t energy, again_energy;
    uint32_t hash, again_hash;

    build(section, song, sizeof song);
    expect(portable_nds_sequence_open(&view, section, SECTION) == PORTABLE_NDS_OK, "open synthetic song");
    const uint32_t ticks = play(&view, 600, &energy, &hash, &status);
    expect(status == PORTABLE_NDS_OK && energy > 0, "note is audible");
    /* Two quarter notes at 120 bpm last one second, about 60 ticks, then the release. */
    expect(ticks > 50 && ticks < 300, "song ends after its note dies away");
    play(&view, 600, &again_energy, &again_hash, &status);
    expect(again_energy == energy && again_hash == hash, "playback is deterministic");

    expect(portable_nds_sequence_open(&view, section, PORTABLE_NDS_SEQUENCE_HEADER_SIZE - 1u) ==
               PORTABLE_NDS_BAD_SECTION,
           "short section rejected");
    section[REGION + 10] = 1;
    expect(portable_nds_sequence_open(&view, section, SECTION) == PORTABLE_NDS_BAD_SECTION,
           "region naming a missing wave rejected");

    uint8_t bad[sizeof song];
    memcpy(bad, song, sizeof song);
    bad[sizeof song - 1] = 0xef;
    build(section, bad, sizeof bad);
    expect(portable_nds_sequence_open(&view, section, SECTION) == PORTABLE_NDS_OK, "open song with bad command");
    play(&view, 600, &energy, &hash, &status);
    expect(status == PORTABLE_NDS_BAD_COMMAND, "unknown command fails loudly");

    const uint8_t loop[] = {0x81, 0x00, 0x3c, 0x7f, 0x18, 0x80, 0x18, 0x94, 0x00, 0x00, 0x00};
    build(section, loop, sizeof loop);
    expect(portable_nds_sequence_open(&view, section, SECTION) == PORTABLE_NDS_OK, "open looping song");
    expect(play(&view, 1200, &energy, &hash, &status) == 1200 && status == PORTABLE_NDS_OK, "jump loops forever");
    /* A 0-length note holds the track until its sound ends, so a retrigger
     * loop (note, jump back to it) waits instead of running away. */
    const uint8_t held[] = {0x81, 0x00, 0x3c, 0x7f, 0x00, 0x94, 0x02, 0x00, 0x00};
    build(section, held, sizeof held);
    expect(portable_nds_sequence_open(&view, section, SECTION) == PORTABLE_NDS_OK, "open held-note loop");
    expect(play(&view, 600, &energy, &hash, &status) == 600 && status == PORTABLE_NDS_OK && energy,
           "held-note retrigger loop plays");

    if (!failures)
        puts("nds sequence: PASS");
    return failures != 0;
}
