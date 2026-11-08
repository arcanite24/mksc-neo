#include "mk64_internal.h"

#include <stdio.h>
#include <string.h>

/* Mario Kart 64 audio (US): the sound driver's instrument banks (ctl), the
 * VADPCM sample table (tbl) and the M64 sequences, located as in
 * n64decomp/mk64 58cfcb0 (assets/audio, src/audio/load.c). Each sequence
 * uses exactly one bank (its gAlBankSets row), so a song section carries
 * that bank whole: portable/audio/n64_sequence.h has the layout. */
#define CTL 0x966260u
#define TBL 0x979AA0u
#define SEQUENCES 0xBC5F60u
#define BANK_SETS 0xBE90E0u
#define SEQUENCE_COUNT 30u
#define BANK_COUNT 21u
#define HEADER 72u

static uint32_t be16(RiCtx *ctx, size_t at) { return mk64_be16(ri_at(ctx, ctx->rom, at, 2)); }
static uint32_t be32(RiCtx *ctx, size_t at) { return mk64_be32(ri_at(ctx, ctx->rom, at, 4)); }

typedef struct
{
    RiCtx *ctx;
    const char *where;
    size_t body, tbl, end; /* bank body and sample table base in the ROM; body end */
    RiBuf *data, *samples;
    uint32_t sample_from[256], envelope_from[256], envelope_to[256], book_from[256], book_to[256];
    uint32_t sample_count, envelope_count, book_count;
} Bank;

static size_t body_at(Bank *b, uint32_t offset, size_t size, const char *what)
{
    if (offset > b->end - b->body || size > b->end - b->body - offset)
        ri_fail(b->ctx, "%s: %s at 0x%X leaves the bank", b->where, what, (unsigned)offset);
    return b->body + offset;
}

/* Copies an envelope (s16 delay/argument pairs up to its terminator). */
static uint32_t envelope(Bank *b, uint32_t offset)
{
    for (uint32_t i = 0; i < b->envelope_count; ++i)
        if (b->envelope_from[i] == offset)
            return b->envelope_to[i];
    if (b->envelope_count >= 256u)
        ri_fail(b->ctx, "%s: more than 256 envelopes", b->where);
    uint32_t points = 0;
    for (;; ++points)
    {
        if (points >= 32u)
            ri_fail(b->ctx, "%s: envelope 0x%X has no end within 32 points", b->where, (unsigned)offset);
        const size_t at = body_at(b, offset + points * 4u, 4, "envelope");
        const int32_t delay = (int16_t)be16(b->ctx, at);
        if (delay == 0 || delay == -1 || delay == -3)
            break;
        if (delay == -2)
        {
            if (be16(b->ctx, at + 2u) > points)
                ri_fail(b->ctx, "%s: envelope 0x%X jumps forward", b->where, (unsigned)offset);
            break;
        }
        if (delay < 0)
            ri_fail(b->ctx, "%s: envelope 0x%X has command %d", b->where, (unsigned)offset, (int)delay);
    }
    const uint32_t to = (uint32_t)b->data->size;
    ri_put(b->ctx, b->data, ri_at(b->ctx, b->ctx->rom, b->body + offset, (points + 1u) * 4u), (points + 1u) * 4u);
    b->envelope_from[b->envelope_count] = offset;
    b->envelope_to[b->envelope_count++] = to;
    return to;
}

static uint32_t book(Bank *b, uint32_t offset)
{
    for (uint32_t i = 0; i < b->book_count; ++i)
        if (b->book_from[i] == offset)
            return b->book_to[i];
    if (b->book_count >= 256u)
        ri_fail(b->ctx, "%s: more than 256 ADPCM books", b->where);
    const size_t at = body_at(b, offset, 8, "ADPCM book");
    const uint32_t order = be32(b->ctx, at), predictors = be32(b->ctx, at + 4u);
    if (order != 2u || !predictors || predictors > 16u)
        ri_fail(b->ctx, "%s: ADPCM book 0x%X has order %u with %u predictors (order 2 only)", b->where,
                (unsigned)offset, (unsigned)order, (unsigned)predictors);
    const size_t size = 8u + predictors * order * 16u;
    body_at(b, offset, size, "ADPCM book");
    const uint32_t to = (uint32_t)b->data->size;
    ri_put(b->ctx, b->data, ri_at(b->ctx, b->ctx->rom, at, size), size);
    b->book_from[b->book_count] = offset;
    b->book_to[b->book_count++] = to;
    return to;
}

/* A bank sound's sample: its index in the section, 0xffff for none. */
static uint32_t sample(Bank *b, uint32_t offset)
{
    if (!offset)
        return 0xffffu;
    for (uint32_t i = 0; i < b->sample_count; ++i)
        if (b->sample_from[i] == offset)
            return i;
    if (b->sample_count >= 256u)
        ri_fail(b->ctx, "%s: more than 256 samples", b->where);
    const size_t at = body_at(b, offset, 20, "sample");
    const uint32_t address = be32(b->ctx, at + 4u), loop = be32(b->ctx, at + 8u), book_offset = be32(b->ctx, at + 12u),
                   size = be32(b->ctx, at + 16u);
    const size_t loop_at = body_at(b, loop, 16, "ADPCM loop");
    const uint32_t start = be32(b->ctx, loop_at), end = be32(b->ctx, loop_at + 4u), count = be32(b->ctx, loop_at + 8u);
    /* Some samples end a few samples into a frame their size stops short of;
     * the driver DMAs whole frames, so take the rest from the table too. */
    const uint32_t frames = (end + 15u) / 16u, length = frames * 9u > size ? frames * 9u : size;
    if (!size || length - size > 9u || (count && start >= end))
        ri_fail(b->ctx, "%s: sample 0x%X loop %u..%u (count %u) does not fit its %u ADPCM bytes", b->where,
                (unsigned)offset, (unsigned)start, (unsigned)end, (unsigned)count, (unsigned)size);
    const uint32_t book_to = book(b, book_offset);
    uint32_t state = 0;
    if (count)
    {
        state = (uint32_t)b->data->size;
        ri_put(b->ctx, b->data, ri_at(b->ctx, b->ctx->rom, body_at(b, loop + 16u, 32, "ADPCM loop state"), 32), 32);
    }
    const uint32_t data = (uint32_t)b->data->size;
    ri_put(b->ctx, b->data, ri_at(b->ctx, b->ctx->rom, b->tbl + address, length), length);
    ri_words(b->ctx, b->samples, 7, data, length, book_to, start, end, count, state);
    b->sample_from[b->sample_count] = offset;
    return b->sample_count++;
}

static void sound(Bank *b, RiBuf *out, size_t at)
{
    ri_u16(b->ctx, out, sample(b, be32(b->ctx, at)));
    ri_u16(b->ctx, out, 0);
    ri_u32(b->ctx, out, be32(b->ctx, at + 4u)); /* tuning, f32 bits */
}

typedef struct
{
    uint8_t player;
    int8_t variation;
    uint16_t follow;
    uint8_t effect_channels[8], bank_players[8];
} SongInfo;

static void song(RiCtx *ctx, uint32_t sequence, const SongInfo *info)
{
    char where[48];
    snprintf(where, sizeof where, "MK64 sequence %u", (unsigned)sequence);
    const size_t entry = SEQUENCES + 4u + sequence * 8u;
    if (be16(ctx, SEQUENCES + 2u) != SEQUENCE_COUNT || sequence >= SEQUENCE_COUNT)
        ri_fail(ctx, "%s: the sequence table at 0x%X does not list 30 sequences", where, SEQUENCES);
    const uint32_t seq_offset = be32(ctx, entry), seq_size = be32(ctx, entry + 4u);
    const uint8_t *commands = ri_at(ctx, ctx->rom, SEQUENCES + seq_offset, seq_size);
    const size_t set = BANK_SETS + be16(ctx, BANK_SETS + sequence * 2u);
    if (ri_at(ctx, ctx->rom, set, 2)[0] != 1u)
        ri_fail(ctx, "%s: uses %u banks (one only)", where, ri_at(ctx, ctx->rom, set, 2)[0]);
    const uint32_t bank_index = ri_at(ctx, ctx->rom, set + 1u, 1)[0];
    if (be16(ctx, CTL + 2u) != BANK_COUNT || bank_index >= BANK_COUNT)
        ri_fail(ctx, "%s: bank %u is not in the 21-bank ctl at 0x%X", where, (unsigned)bank_index, CTL);
    const uint32_t bank_offset = be32(ctx, CTL + 4u + bank_index * 8u), bank_size = be32(ctx, CTL + 8u + bank_index * 8u);
    ri_at(ctx, ctx->rom, CTL + bank_offset, bank_size);
    Bank b = {.ctx = ctx, .where = where, .body = CTL + bank_offset + 16u, .end = CTL + bank_offset + bank_size,
              .tbl = TBL + be32(ctx, TBL + 4u + bank_index * 8u), .data = ri_buf(ctx), .samples = ri_buf(ctx)};
    if (bank_size < 20u)
        ri_fail(ctx, "%s: bank %u is %u bytes", where, (unsigned)bank_index, (unsigned)bank_size);
    const uint32_t instruments = be32(ctx, CTL + bank_offset), drums = be32(ctx, CTL + bank_offset + 4u);
    if (instruments > 0x7fu || drums > 0x100u)
        ri_fail(ctx, "%s: bank %u has %u instruments and %u drums", where, (unsigned)bank_index,
                (unsigned)instruments, (unsigned)drums);
    RiBuf *inst = ri_buf(ctx), *drum = ri_buf(ctx);
    for (uint32_t i = 0; i < instruments; ++i)
    {
        const uint32_t pointer = be32(ctx, body_at(&b, 4u + i * 4u, 4, "instrument list"));
        if (!pointer)
        {
            ri_put_zero(ctx, inst, 32);
            for (unsigned s = 0; s < 3u; ++s)
                ri_set_u16(inst, inst->size - 24u + s * 8u, 0xffffu);
            continue;
        }
        const size_t at = body_at(&b, pointer, 32, "instrument");
        const uint8_t *r = ri_at(ctx, ctx->rom, at, 4);
        ri_u8(ctx, inst, 1);
        ri_u8(ctx, inst, r[1]);
        ri_u8(ctx, inst, r[2]);
        ri_u8(ctx, inst, r[3]);
        ri_u32(ctx, inst, envelope(&b, be32(ctx, at + 4u)));
        for (unsigned s = 0; s < 3u; ++s)
            sound(&b, inst, at + 8u + s * 8u);
    }
    const uint32_t drum_list = drums ? be32(ctx, body_at(&b, 0, 4, "drum list")) : 0u;
    for (uint32_t i = 0; i < drums; ++i)
    {
        const uint32_t pointer = be32(ctx, body_at(&b, drum_list + i * 4u, 4, "drum list"));
        if (!pointer)
        {
            ri_put_zero(ctx, drum, 16);
            ri_set_u16(drum, drum->size - 8u, 0xffffu);
            continue;
        }
        const size_t at = body_at(&b, pointer, 16, "drum");
        const uint8_t *r = ri_at(ctx, ctx->rom, at, 2);
        ri_u8(ctx, drum, 1);
        ri_u8(ctx, drum, r[0]);
        ri_u8(ctx, drum, r[1]);
        ri_u8(ctx, drum, 0);
        ri_u32(ctx, drum, envelope(&b, be32(ctx, at + 12u)));
        sound(&b, drum, at + 4u);
    }
    RiBuf *out = ri_buf(ctx);
    const uint32_t seq_at = HEADER, inst_at = seq_at + ((seq_size + 3u) & ~3u);
    const uint32_t drum_at = inst_at + (uint32_t)inst->size, sample_at = drum_at + (uint32_t)drum->size;
    const uint32_t data_at = sample_at + (uint32_t)b.samples->size;
    ri_put(ctx, out, "MKSCN64S", 8);
    ri_u16(ctx, out, HEADER);
    ri_u16(ctx, out, 1);
    ri_u8(ctx, out, info->player);
    ri_u8(ctx, out, (uint8_t)info->variation);
    ri_u16(ctx, out, info->follow);
    ri_put(ctx, out, info->effect_channels, 8);
    ri_words(ctx, out, 10, seq_at, seq_size, inst_at, instruments, drum_at, drums, sample_at, b.sample_count, data_at,
             (uint32_t)b.data->size);
    ri_put(ctx, out, info->bank_players, 8);
    ri_put(ctx, out, commands, seq_size);
    ri_put_zero(ctx, out, inst_at - seq_at - seq_size);
    ri_put_buf(ctx, out, inst);
    ri_put_buf(ctx, out, drum);
    ri_put_buf(ctx, out, b.samples);
    ri_put_buf(ctx, out, b.data);
    ri_section(ctx, MK64_SONG_KIND, MK64_SONG_ID(sequence), 1, out);
}

/* ---- Course music, events, effects ------------------------------------------- */

/* Kind-114 sound set (portable/assets/sound_set_asset.h). */
#define SOUND_SET_KIND 114u
enum { CUE_ENGINE_IDLE = 0xff01, CUE_ENGINE_COAST, CUE_ENGINE_THROTTLE, CUE_DRIFT };

enum
{
    SEQ_EFFECTS = 0, SEQ_FINAL_LAP = 12, SEQ_FINISH_FIRST = 13, SEQ_FINISH_PLACED = 14, SEQ_FINISH_LOST = 15,
    SEQ_RESULTS = 16, SEQ_STAR = 17, SEQ_VERSUS_WIN = 23, SEQ_RESULTS_LOST = 24,
};

/* Course music by course index (race_logic.c play_course_music / the
 * battle branch): raceways and Wario Stadium share 3, and so on. */
static const uint8_t course_music[20] = {3, 5, 9, 7, 4, 8, 6, 3, 3, 4, 21, 10, 8, 18, 3, 5, 25, 5, 19, 25};

uint32_t mk64_course_song(uint32_t index) { return index < 20u ? MK64_SONG_ID(course_music[index]) : 0u; }

/* Sound banks of the one-player effect sequence (seq 0, variation 0): its
 * script starts channels 0-3 for bank 0, 4-5 bank 1 (kart), 6-7 bank 2
 * (voices), 8-9 bank 3, 10-11 bank 4 (race events), 12 bank 5 (D_800EA188). */
static const uint8_t effect_channels[8] = {0x04, 0x42, 0x62, 0x82, 0xa2, 0xc1, 0, 0};
/* Default session slots of each bank's sounds (stock voices play on 2). */
static const uint8_t effect_players[8] = {6, 6, 2, 6, 7, 10, 6, 6};

/* MK64 characterId of each imported racer, in MK64_DRIVER_ID order
 * (Mario, Luigi, Peach, Toad, Yoshi, DK, Wario, Bowser). */
static const uint8_t character[8] = {0, 1, 6, 3, 2, 4, 5, 7};
enum { VOICE_BOOST = 1, VOICE_STRUCK = 8, VOICES_PER_CHARACTER = 16 };

/* Stock cue -> MK64 sound. MK64 sound IDs are bank << 28 | id (sounds.h);
 * voices are bank 2, sixteen per character; engines are bank 0, one per
 * character. Kind-114 cues without a row keep their stock sound: landing
 * (159), the lap cue (107), banana landed (122), recovery (133), respawn
 * (138), spin squeal (162), Boo (129), coins (101), bombs (386-389), course
 * hazards (169, 170, 371-389) and the drift spark tiers have no MK64 sound
 * identified yet. MK64 has no lightning, Boo or haunt music, so those
 * songs are dropped and the course music continues. */
static const uint16_t sound_cues[][2] = {
    {114, MK64_EFFECT(4, 0x03)}, {115, MK64_EFFECT(4, 0x04)}, /* countdown light, green light */
    {157, MK64_EFFECT(1, 0x00)},                               /* hop */
    {140, MK64_EFFECT(1, 0x1e)},                               /* mini-turbo or boost panel */
    {125, MK64_EFFECT(1, 0x0b)},                               /* mushroom boost */
    {121, MK64_EFFECT(1, 0x06)},                               /* item box */
    {103, MK64_EFFECT(0, 0x1c)}, {104, MK64_EFFECT(0, 0x47)}, /* roulette, item decided */
    {123, MK64_EFFECT(1, 0x04)}, {134, MK64_EFFECT(1, 0x12)}, /* shell launch, banana drop */
    {124, MK64_EFFECT(1, 0x54)},                               /* shell hit */
    {132, MK64_EFFECT(1, 0x4b)}, {135, MK64_EFFECT(1, 0x4b)}, /* hit by an item */
    {126, MK64_EFFECT(5, 0x0c)},                               /* thunderbolt */
    {161, MK64_EFFECT(0, 0x28)},                               /* Lakitu */
    {108, MK64_EFFECT(1, 0x03)},                               /* finish line */
    {CUE_DRIFT, MK64_EFFECT(0, 0x08)},          /* tyre squeal */
    {CUE_ENGINE_IDLE, MK64_EFFECT(0, 0)},       /* Mario's engine for unvoiced racers */
    {CUE_ENGINE_COAST, MK64_EFFECT(0, 0)},
    {CUE_ENGINE_THROTTLE, MK64_EFFECT(0, 0)},
    {19, MK64_SONG(SEQ_FINAL_LAP)}, {10, MK64_SONG(SEQ_STAR)},
    {11, 0}, {12, 0}, {13, 0},                                 /* lightning, Boo, haunt music */
    {5, MK64_SONG(SEQ_FINISH_FIRST)}, {6, MK64_SONG(SEQ_FINISH_PLACED)}, {7, MK64_SONG(SEQ_FINISH_LOST)},
    {33, MK64_SONG(SEQ_VERSUS_WIN)}, {34, MK64_SONG(SEQ_VERSUS_WIN)},
};

/* Engine pitch: Mario's profile (0.35 + rpm / 1569 freqScale) from rest
 * to about 1.6x at top speed, in 1/256 semitone. */
#define ENGINE_PITCH_BASE (-4652)
#define ENGINE_PITCH_RANGE 6740u

static void sound_set(RiCtx *ctx)
{
    const uint32_t cues = (uint32_t)(sizeof sound_cues / sizeof sound_cues[0]);
    RiBuf *out = ri_buf(ctx);
    ri_put(ctx, out, "MKSCSND1", 8);
    ri_u16(ctx, out, 24);
    ri_u16(ctx, out, 2);
    ri_u16(ctx, out, cues);
    ri_u16(ctx, out, 8);
    ri_u16(ctx, out, ENGINE_PITCH_RANGE);
    ri_i16(ctx, out, ENGINE_PITCH_BASE);
    ri_u32(ctx, out, 0);
    for (uint32_t i = 0; i < cues; ++i)
    {
        ri_u16(ctx, out, sound_cues[i][0]);
        ri_u16(ctx, out, sound_cues[i][1]);
    }
    for (uint32_t i = 0; i < 8u; ++i)
    {
        const uint32_t first = character[i] * VOICES_PER_CHARACTER;
        ri_u32(ctx, out, MK64_DRIVER_ID(i));
        ri_u16(ctx, out, MK64_EFFECT(2, first + VOICE_BOOST));
        ri_u16(ctx, out, MK64_EFFECT(2, first + VOICE_STRUCK));
        ri_u16(ctx, out, MK64_EFFECT(0, character[i]));
        ri_u16(ctx, out, 0);
    }
    ri_section(ctx, SOUND_SET_KIND, MK64_SOUND_SET_ID, 2, out);
}

void mk64_sounds(RiCtx *ctx)
{
    SongInfo effects = {.player = effect_players[0], .variation = 0};
    memcpy(effects.effect_channels, effect_channels, sizeof effect_channels);
    memcpy(effects.bank_players, effect_players, sizeof effect_players);
    song(ctx, SEQ_EFFECTS, &effects);
    uint8_t done[SEQUENCE_COUNT] = {0};
    for (unsigned i = 0; i < 20u; ++i)
        if (!done[course_music[i]]++)
            song(ctx, course_music[i], &(SongInfo){.variation = -1});
    static const uint8_t events[][2] = {
        {SEQ_FINAL_LAP, 0}, {SEQ_STAR, 0}, {SEQ_VERSUS_WIN, 0},
        {SEQ_FINISH_FIRST, SEQ_RESULTS}, {SEQ_FINISH_PLACED, SEQ_RESULTS}, {SEQ_FINISH_LOST, SEQ_RESULTS_LOST},
        {SEQ_RESULTS, 0}, {SEQ_RESULTS_LOST, 0},
    };
    for (unsigned i = 0; i < sizeof events / sizeof events[0]; ++i)
        song(ctx, events[i][0],
             &(SongInfo){.variation = -1, .follow = (uint16_t)(events[i][1] ? MK64_SONG(events[i][1]) : 0u)});
    sound_set(ctx);
}
