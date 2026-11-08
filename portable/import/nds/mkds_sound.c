#include "mkds_sound.h"

#include <stdio.h>
#include <string.h>

#define WAVE_LIMIT 1024u

typedef struct
{
    RiCtx *ctx;
    RiData sdat;
    char where[48];
    RiData archives[4];
    uint32_t archive_ids[4];
    /* Waves kept, as (archive slot << 16 | SWAV index). */
    uint32_t wave_keys[WAVE_LIMIT];
    uint32_t wave_count;
    RiBuf *programs, *regions, *waves, *wave_data;
    uint32_t region_count;
} Sound;

static void check_magic(Sound *s, RiData file, const char *magic, const char *what)
{
    if (memcmp(ri_at(s->ctx, file, 0, 4), magic, 4))
        ri_fail(s->ctx, "%s: %s is not %s", s->where, what, magic);
}

/* INFO record `kind` entry `index`, as a view from its start. */
static RiData info_entry(Sound *s, uint32_t kind, uint32_t index, size_t size)
{
    const uint32_t info = ri_get_u32(s->ctx, s->sdat, 0x18);
    const uint32_t record = info + ri_get_u32(s->ctx, s->sdat, info + 8u + kind * 4u);
    if (index >= ri_get_u32(s->ctx, s->sdat, record))
        ri_fail(s->ctx, "%s: INFO record %u has no entry %u", s->where, kind, index);
    const uint32_t entry = ri_get_u32(s->ctx, s->sdat, record + 4u + index * 4u);
    if (!entry)
        ri_fail(s->ctx, "%s: INFO record %u entry %u is empty", s->where, kind, index);
    return ri_span(s->ctx, s->sdat, info + entry, size);
}

static RiData sdat_file(Sound *s, uint32_t file)
{
    const uint32_t fat = ri_get_u32(s->ctx, s->sdat, 0x20);
    if (file >= ri_get_u32(s->ctx, s->sdat, fat + 8u))
        ri_fail(s->ctx, "%s: file %u is past the FAT", s->where, file);
    return ri_span(s->ctx, s->sdat, ri_get_u32(s->ctx, s->sdat, fat + 12u + file * 16u),
                   ri_get_u32(s->ctx, s->sdat, fat + 16u + file * 16u));
}

/* Index of the kept wave for (archive slot, SWAV), copying it on first use. */
static uint32_t keep_wave(Sound *s, uint32_t slot, uint32_t swav)
{
    if (slot >= 4u || !s->archives[slot].data)
        ri_fail(s->ctx, "%s: instrument names missing wave archive slot %u", s->where, slot);
    const uint32_t key = slot << 16 | swav;
    for (uint32_t i = 0; i < s->wave_count; ++i)
        if (s->wave_keys[i] == key)
            return i;
    if (s->wave_count == WAVE_LIMIT)
        ri_fail(s->ctx, "%s uses more than %u waves", s->where, WAVE_LIMIT);
    const RiData swar = s->archives[slot];
    if (swav >= ri_get_u32(s->ctx, swar, 0x38))
        ri_fail(s->ctx, "%s: wave archive %u has no wave %u", s->where, s->archive_ids[slot], swav);
    const uint32_t at = ri_get_u32(s->ctx, swar, 0x3c + swav * 4u);
    const uint32_t encoding = ri_get_u8(s->ctx, swar, at), loop = ri_get_u8(s->ctx, swar, at + 1u);
    const uint32_t timer = ri_get_u16(s->ctx, swar, at + 4u), loop_words = ri_get_u16(s->ctx, swar, at + 6u);
    const uint32_t tail_words = ri_get_u32(s->ctx, swar, at + 8u);
    const uint64_t bytes = ((uint64_t)loop_words + tail_words) * 4u;
    if (encoding > 2u || loop > 1u || !timer || bytes <= (encoding == 2u ? 4u : 0u) || bytes > 0x1000000u)
        ri_fail(s->ctx, "%s: wave %u/%u has unsupported encoding %u, loop %u or size", s->where,
                s->archive_ids[slot], swav, encoding, loop);
    /* Samples per byte after the ADPCM header: 2 ADPCM, 1 PCM8, 1/2 PCM16. */
    const uint32_t header = encoding == 2u ? 4u : 0u;
    const uint32_t count = encoding == 0u ? (uint32_t)bytes : encoding == 1u ? (uint32_t)bytes / 2u : (uint32_t)(bytes - 4u) * 2u;
    const uint32_t loop_bytes = loop_words * 4u;
    if (loop && loop_bytes < header)
        ri_fail(s->ctx, "%s: wave %u/%u loops inside its ADPCM header", s->where, s->archive_ids[slot], swav);
    const uint32_t loop_start = !loop ? 0u : encoding == 0u ? loop_bytes : encoding == 1u ? loop_bytes / 2u : (loop_bytes - 4u) * 2u;
    if (loop && loop_start >= count)
        ri_fail(s->ctx, "%s: wave %u/%u loop starts past its end", s->where, s->archive_ids[slot], swav);
    const RiData data = ri_span(s->ctx, swar, at + 12u, (size_t)bytes);
    /* Data offsets are fixed up once the header and tables are sized. */
    ri_words(s->ctx, s->waves, 6, encoding | loop << 8, timer, loop_start, count,
             (uint32_t)s->wave_data->size, (uint32_t)bytes);
    ri_put(s->ctx, s->wave_data, data.data, data.size);
    ri_resize(s->ctx, s->wave_data, (s->wave_data->size + 3u) & ~(size_t)3u);
    s->wave_keys[s->wave_count] = key;
    return s->wave_count++;
}

/* One 10-byte SBNK instrument covering keys low..high. */
static void region(Sound *s, uint32_t type, RiData note, uint32_t low, uint32_t high, uint32_t program)
{
    const uint8_t *n = ri_at(s->ctx, note, 0, 10);
    uint32_t kind, wave = 0;
    switch (type)
    {
    case 1:
        kind = 1;
        wave = keep_wave(s, ri_le16(n + 2), ri_le16(n));
        break;
    case 2:
        kind = 2;
        wave = ri_le16(n);
        if (wave > 6u)
            ri_fail(s->ctx, "%s: program %u PSG duty %u", s->where, program, wave);
        break;
    case 3:
        kind = 3;
        break;
    default:
        ri_fail(s->ctx, "%s: program %u has unsupported instrument type %u", s->where, program, type);
    }
    if (n[4] > 127u || n[5] > 127u || n[6] > 127u || n[7] > 127u || n[8] > 127u || n[9] > 127u)
        ri_fail(s->ctx, "%s: program %u has out-of-range key, envelope or pan", s->where, program);
    const uint8_t row[12] = {(uint8_t)low, (uint8_t)high, (uint8_t)kind, n[4], n[5], n[6], n[7], n[8], n[9], 0,
                             (uint8_t)wave, (uint8_t)(wave >> 8)};
    ri_put(s->ctx, s->regions, row, sizeof row);
    s->region_count++;
}

static void program(Sound *s, RiData bank, uint32_t index)
{
    const uint32_t record = 0x3c + index * 4u;
    const uint32_t type = ri_get_u8(s->ctx, bank, record), offset = ri_get_u16(s->ctx, bank, record + 1u);
    const uint32_t first = s->region_count;
    if (type >= 1u && type <= 3u)
        region(s, type, ri_span(s->ctx, bank, offset, 10), 0, 127, index);
    else if (type == 16u)
    {
        const uint32_t low = ri_get_u8(s->ctx, bank, offset), high = ri_get_u8(s->ctx, bank, offset + 1u);
        if (low > high || high > 127u)
            ri_fail(s->ctx, "%s: drum set %u spans keys %u..%u", s->where, index, low, high);
        for (uint32_t key = low; key <= high; ++key)
        {
            const uint32_t at = offset + 2u + (key - low) * 12u;
            const uint32_t note_type = ri_get_u16(s->ctx, bank, at);
            if (note_type)
                region(s, note_type, ri_span(s->ctx, bank, at + 2u, 10), key, key, index);
        }
    }
    else if (type == 17u)
    {
        uint32_t low = 0, at = offset + 8u;
        for (uint32_t i = 0; i < 8u; ++i)
        {
            const uint32_t high = ri_get_u8(s->ctx, bank, offset + i);
            if (!high)
                break;
            if (high < low || high > 127u)
                ri_fail(s->ctx, "%s: key split %u region %u ends at key %u", s->where, index, i, high);
            region(s, ri_get_u16(s->ctx, bank, at), ri_span(s->ctx, bank, at + 2u, 10), low, high, index);
            low = high + 1u;
            at += 12u;
        }
    }
    else if (type)
        ri_fail(s->ctx, "%s: program %u has unsupported bank record type %u", s->where, index, type);
    ri_u16(s->ctx, s->programs, first);
    ri_u16(s->ctx, s->programs, s->region_count - first);
}

/* ---- Sequence reach -------------------------------------------------------- */

/* The commands an archive entry can reach from its start: their byte span,
 * the 3-byte targets to rebase when the span is copied out, and the programs
 * they select (all of them when one is chosen by a variable or at random). */
typedef struct
{
    Sound *s;
    RiData block;
    uint8_t *seen;
    RiBuf *pending, *targets;
    uint8_t *programs;
    uint32_t program_count, low, high;
    int every_program;
} Reach;

static uint32_t reach_byte(Reach *r, uint32_t *at)
{
    if (*at >= r->block.size)
        ri_fail(r->s->ctx, "%s: commands run past the archive", r->s->where);
    return r->block.data[(*at)++];
}

static uint32_t reach_varlen(Reach *r, uint32_t *at)
{
    uint32_t value = 0, byte;
    unsigned count = 0;
    do
    {
        if (++count > 4u)
            ri_fail(r->s->ctx, "%s: variable-length argument too long", r->s->where);
        byte = reach_byte(r, at);
        value = value << 7 | (byte & 0x7fu);
    } while (byte & 0x80u);
    return value;
}

static void reach_push(Reach *r, uint32_t at, uint32_t target_at)
{
    if (at >= r->block.size)
        ri_fail(r->s->ctx, "%s: jump to %u past the archive", r->s->where, at);
    if (target_at != UINT32_MAX)
        ri_u32(r->s->ctx, r->targets, target_at);
    ri_u32(r->s->ctx, r->pending, at);
}

/* An argument of `size` bytes (0: variable length), as replaced by a random
 * (0xa0: two s16) or variable (0xa1: one u8) prefix. Returns UINT32_MAX when
 * the value is not known here. */
static uint32_t reach_argument(Reach *r, uint32_t *at, unsigned size, uint32_t prefix)
{
    if (prefix)
    {
        for (unsigned i = 0; i < (prefix == 0xa0u ? 4u : 1u); ++i)
            reach_byte(r, at);
        return UINT32_MAX;
    }
    if (!size)
        return reach_varlen(r, at);
    uint32_t value = 0;
    for (unsigned i = 0; i < size; ++i)
        value |= reach_byte(r, at) << (8u * i);
    return value;
}

static void reach(Reach *r, uint32_t start)
{
    r->low = r->high = start;
    reach_push(r, start, UINT32_MAX);
    while (r->pending->size)
    {
        r->pending->size -= 4u;
        uint32_t at = ri_le32(r->pending->data + r->pending->size);
        for (int running = 1; running && !r->seen[at];)
        {
            r->seen[at] = 1;
            r->low = at < r->low ? at : r->low;
            uint32_t command = reach_byte(r, &at), prefix = 0;
            while (command >= 0xa0u && command <= 0xa2u)
            {
                if (command != 0xa2u)
                    prefix = command;
                command = reach_byte(r, &at);
            }
            if (command < 0x80u)
            {
                reach_byte(r, &at);
                reach_argument(r, &at, 0, prefix);
            }
            else if (command == 0x80u || command == 0x81u)
            {
                const uint32_t value = reach_argument(r, &at, 0, prefix);
                if (command == 0x81u && value == UINT32_MAX)
                    r->every_program = 1;
                else if (command == 0x81u && value < r->program_count)
                    r->programs[value] = 1;
            }
            else if (command == 0x93u || command == 0x94u || command == 0x95u)
            {
                if (command == 0x93u)
                    reach_byte(r, &at);
                const uint32_t target_at = at, target = reach_argument(r, &at, 3, 0);
                reach_push(r, target, target_at);
                running = command != 0x94u;
            }
            else if (command >= 0xb0u && command <= 0xbdu && command != 0xb7u)
            {
                reach_byte(r, &at);
                reach_argument(r, &at, 2, prefix);
            }
            else if (command >= 0xc0u && command <= 0xd6u)
                reach_argument(r, &at, 1, prefix);
            else if (command == 0xe0u || command == 0xe1u || command == 0xe3u || command == 0xfeu)
                reach_argument(r, &at, 2, prefix);
            else if (command == 0xfdu || command == 0xffu)
                running = 0;
            else if (command != 0xfcu)
                ri_fail(r->s->ctx, "%s: unknown command 0x%02X at %u", r->s->where, command, at - 1u);
            r->high = at > r->high ? at : r->high;
            if (at >= r->block.size && running)
                ri_fail(r->s->ctx, "%s: commands run past the archive", r->s->where);
        }
    }
}

/* ---- Sections --------------------------------------------------------------- */

/* Writes a kind-112 section for `commands` started at `start`, with the
 * programs of SDAT bank `bank_index` that `used` marks (NULL: all). */
static void sound_section(Sound *s, RiData commands, uint32_t start, uint32_t bank_index, uint32_t volume,
                          uint32_t player, const uint8_t *used, uint32_t id, uint32_t follow)
{
    RiCtx *ctx = s->ctx;
    const RiData bank_info = info_entry(s, 2, bank_index, 12);
    const RiData bank = sdat_file(s, ri_get_u16(ctx, bank_info, 0));
    check_magic(s, bank, "SBNK", "its bank");
    for (uint32_t slot = 0; slot < 4u; ++slot)
    {
        const uint32_t archive = ri_get_u16(ctx, bank_info, 4u + slot * 2u);
        if (archive == 0xffffu)
            continue;
        s->archive_ids[slot] = archive;
        s->archives[slot] = sdat_file(s, ri_get_u32(ctx, info_entry(s, 3, archive, 4), 0) & 0xffffffu);
        check_magic(s, s->archives[slot], "SWAR", "a wave archive");
    }
    s->programs = ri_buf(ctx);
    s->regions = ri_buf(ctx);
    s->waves = ri_buf(ctx);
    s->wave_data = ri_buf(ctx);
    const uint32_t programs = ri_get_u32(ctx, bank, 0x38);
    if (!programs || programs > 65536u)
        ri_fail(ctx, "%s: bank %u has %u programs", s->where, bank_index, programs);
    for (uint32_t i = 0; i < programs; ++i)
        if (!used || used[i])
            program(s, bank, i);
        else
        {
            ri_u16(ctx, s->programs, (uint16_t)s->region_count);
            ri_u16(ctx, s->programs, 0);
        }
    const uint32_t header = start ? 52u : 48u;
    const uint32_t sequence_at = header, program_at = sequence_at + (((uint32_t)commands.size + 3u) & ~3u);
    const uint32_t region_at = program_at + (uint32_t)s->programs->size;
    const uint32_t wave_at = region_at + (((uint32_t)s->regions->size + 3u) & ~3u);
    const uint32_t data_at = wave_at + (uint32_t)s->waves->size;
    for (uint32_t i = 0; i < s->wave_count; ++i)
        ri_set_u32(s->waves, i * 24u + 16u, data_at + ri_le32(s->waves->data + i * 24u + 16u));
    RiBuf *out = ri_buf(ctx);
    ri_put(ctx, out, "MKSCNSQ1", 8);
    ri_u16(ctx, out, (uint16_t)header);
    ri_u16(ctx, out, start ? 2u : 1u);
    ri_u8(ctx, out, (uint8_t)volume);
    ri_u8(ctx, out, (uint8_t)player);
    ri_u16(ctx, out, (uint16_t)follow);
    ri_words(ctx, out, 8, sequence_at, (uint32_t)commands.size, program_at, programs, region_at, s->region_count,
             wave_at, s->wave_count);
    if (start)
        ri_u32(ctx, out, start);
    ri_put(ctx, out, commands.data, commands.size);
    ri_resize(ctx, out, program_at);
    ri_put_buf(ctx, out, s->programs);
    ri_put_buf(ctx, out, s->regions);
    ri_resize(ctx, out, wave_at);
    ri_put_buf(ctx, out, s->waves);
    ri_put_buf(ctx, out, s->wave_data);
    ri_section(ctx, MKDS_SOUND_SECTION_KIND, id, start ? 2u : 1u, out);
}

void mkds_sound_sequence(RiCtx *ctx, RiData sdat, uint32_t sequence, uint32_t follow)
{
    Sound s = {.ctx = ctx, .sdat = sdat};
    snprintf(s.where, sizeof(s.where), "SDAT sequence %u", sequence);
    check_magic(&s, sdat, "SDAT", "sound_data.sdat");
    const RiData info = info_entry(&s, 0, sequence, 12);
    const RiData sseq = sdat_file(&s, ri_get_u16(ctx, info, 0));
    check_magic(&s, sseq, "SSEQ", "its sequence");
    const uint32_t start = ri_get_u32(ctx, sseq, 0x18);
    if (start >= sseq.size)
        ri_fail(ctx, "%s: sequence data starts past the file", s.where);
    sound_section(&s, ri_span(ctx, sseq, start, sseq.size - start), 0, ri_get_u16(ctx, info, 4),
                  ri_get_u8(ctx, info, 6), 0 /* course music player */, NULL, MKDS_MUSIC_ID(sequence), follow);
}

void mkds_sound_effect(RiCtx *ctx, RiData sdat, uint32_t archive, uint32_t index, uint32_t player, uint32_t id)
{
    Sound s = {.ctx = ctx, .sdat = sdat};
    snprintf(s.where, sizeof(s.where), "SDAT effect archive %u entry %u", archive, index);
    check_magic(&s, sdat, "SDAT", "sound_data.sdat");
    const RiData ssar = sdat_file(&s, ri_get_u16(ctx, info_entry(&s, 1, archive, 4), 0));
    check_magic(&s, ssar, "SSAR", "its archive");
    const uint32_t data = ri_get_u32(ctx, ssar, 0x18), count = ri_get_u32(ctx, ssar, 0x1c);
    if (index >= count || data >= ssar.size)
        ri_fail(ctx, "%s: the archive holds %u entries", s.where, count);
    const RiData entry = ri_span(ctx, ssar, 0x20u + index * 12u, 12);
    const uint32_t bank_index = ri_get_u16(ctx, entry, 4);
    const RiData bank = sdat_file(&s, ri_get_u16(ctx, info_entry(&s, 2, bank_index, 12), 0));
    Reach r = {.s = &s, .block = ri_span(ctx, ssar, data, ssar.size - data), .pending = ri_buf(ctx),
               .targets = ri_buf(ctx), .program_count = ri_get_u32(ctx, bank, 0x38)};
    if (r.program_count > 65536u)
        ri_fail(ctx, "%s: bank %u has %u programs", s.where, bank_index, r.program_count);
    r.seen = ri_buf_zero(ctx, r.block.size)->data;
    r.programs = ri_buf_zero(ctx, r.program_count + 1u)->data;
    reach(&r, ri_get_u32(ctx, entry, 0));
    /* Copy the reached span; its jumps, calls and track opens are absolute
     * within the archive's block, so rebase them onto the copy. */
    RiBuf *commands = ri_buf(ctx);
    ri_put(ctx, commands, r.block.data + r.low, r.high - r.low);
    for (size_t i = 0; i < r.targets->size; i += 4u)
    {
        const uint32_t at = ri_le32(r.targets->data + i) - r.low;
        const uint32_t target = (commands->data[at] | commands->data[at + 1u] << 8 | (uint32_t)commands->data[at + 2u] << 16) - r.low;
        commands->data[at] = (uint8_t)target;
        commands->data[at + 1u] = (uint8_t)(target >> 8);
        commands->data[at + 2u] = (uint8_t)(target >> 16);
    }
    const uint32_t start = ri_get_u32(ctx, entry, 0) - r.low;
    /* v2 marks a start; an entry that begins its span starts at 0 in v1. */
    sound_section(&s, (RiData){commands->data, commands->size}, start, bank_index, ri_get_u8(ctx, entry, 6), player,
                  r.every_program ? NULL : r.programs, id, 0u);
}

/* ---- Sound set ---------------------------------------------------------------- */

/* Kind-114 cues without a stock song (portable/assets/sound_set_asset.h). */
enum
{
    CUE_ENGINE_IDLE = 0xff01, CUE_ENGINE_COAST, CUE_ENGINE_THROTTLE, CUE_DRIFT, CUE_SPARK_FIRST, CUE_SPARK_SECOND,
    CUE_LAP, CUE_SURFACE_DRIFT = 0xfc00, CUE_SURFACE_LAND = 0xfc40, CUE_SURFACE_RUN = 0xfc80,
    SONG = 0x8000, /* a kind-112 song */
};

/* Effect-archive entries by their names in the game (SSAR 0); voices are
 * SSAR 2, fourteen per character. */
enum
{
    SE_PAUSE = 17, SE_SIGNAL321 = 39, SE_SIGNAL_GO = 40, SE_ROULETTE = 62, SE_ITEM_DECIDE = 63,
    SE_SPECIAL_DECIDE = 64, SE_LAP = 65, SE_GOAL = 66, SE_SLIP_ASPHALT = 84, SE_LAND_ASPHALT = 103,
    SE_DASH = 160, SE_KART_IDLE = 170, SE_KART_ACCEL = 173, SE_KART_DECEL = 174, SE_MINI_JUMP = 207,
    SE_TURBO_FULL = 209, SE_TURBO_FIRST = 210, SE_ITEMBOX = 212, SE_SHELL_HIT = 217, SE_ITEM_THROW = 218,
    SE_BANANA_LAND = 219, SE_THUNDER = 222, SE_EXPLOSION = 223, SE_STAR_HIT = 232, SE_RESCUE = 258,
    VOICES_PER_CHARACTER = 14,
};

/* DriverVoiceType: the fourteen voices of a character. */
enum
{
    VOICE_SPIN, VOICE_OT, VOICE_OT1, VOICE_OT2, VOICE_OT3, VOICE_STARTDASH, VOICE_STARTDASH_OT, VOICE_ATTACK,
    VOICE_JOY, VOICE_GOAL1, VOICE_GOAL23, VOICE_GOAL48, VOICE_MG_WIN, VOICE_MG_LOSE, VOICE_NONE = 0xff,
};

/* Sequences (SDAT INFO record 0): the final-lap fanfare, star music, the goal
 * fanfares and the results music they lead into (the game preloads 46, 47,
 * 48, 56 and 57 for a Grand Prix). */
enum
{
    SEQ_GOAL_FIRST = 46, SEQ_GOAL_PLACED = 47, SEQ_GOAL_LOST = 48, SEQ_RESULT_WIN = 56, SEQ_RESULT_LOSE = 57,
    SEQ_FINAL_LAP = 62, SEQ_STAR = 73,
};
static const uint8_t event_sequences[][2] = {
    {SEQ_FINAL_LAP, 0}, {SEQ_STAR, 0}, {SEQ_GOAL_FIRST, SEQ_RESULT_WIN}, {SEQ_GOAL_PLACED, SEQ_RESULT_WIN},
    {SEQ_GOAL_LOST, SEQ_RESULT_LOSE}, {SEQ_RESULT_WIN, 0}, {SEQ_RESULT_LOSE, 0},
};
/* The final lap plays the course music at 300/256 of its tempo
 * (the ARM9 star-music stop path, 0x2106ce4). */
#define FINAL_LAP_TEMPO 300u

/* Stock cue -> DS effect (an SSAR 0 entry), DS sequence (| SONG) or 0 for
 * silence. Stock cues with no DS sound are silenced: coins (101), the item
 * slot reset (131), Boo (129), recovery (133), the fall (138), the spin
 * squeal (162), 136, 164 and the GBA course hazards (169, 170, 380, 384);
 * Boo, lightning and haunt music (11, 12, 13) drop and the course music
 * continues. */
static const uint16_t sound_cues[][2] = {
    {CUE_ENGINE_IDLE, SE_KART_IDLE}, {CUE_ENGINE_COAST, SE_KART_DECEL},
    {CUE_ENGINE_THROTTLE, SE_KART_ACCEL}, {CUE_DRIFT, SE_SLIP_ASPHALT},
    {CUE_SPARK_FIRST, SE_TURBO_FIRST}, {CUE_SPARK_SECOND, SE_TURBO_FULL},
    {CUE_LAP, SE_LAP},
    {114, SE_SIGNAL321}, {115, SE_SIGNAL_GO},     /* countdown, go */
    {157, SE_MINI_JUMP}, {159, SE_LAND_ASPHALT},  /* hop, landing */
    {140, SE_DASH}, {125, SE_DASH},               /* mini-turbo or panel, mushroom */
    {161, SE_RESCUE},                             /* Lakitu */
    {121, SE_ITEMBOX}, {122, SE_BANANA_LAND},     /* box broken, banana landed */
    {103, SE_ROULETTE}, {104, SE_ITEM_DECIDE},    /* roulette, item decided */
    {130, SE_SPECIAL_DECIDE},                     /* special item decided */
    {123, SE_ITEM_THROW}, {134, SE_ITEM_THROW},   /* shell thrown, banana dropped */
    {124, SE_SHELL_HIT}, {132, SE_SHELL_HIT},     /* shell impact, racer struck */
    {135, SE_SHELL_HIT},                          /* banana slip */
    {160, SE_STAR_HIT},                           /* star ram */
    {386, SE_EXPLOSION}, {126, SE_THUNDER},       /* bomb, lightning */
    {108, SE_GOAL}, {142, SE_PAUSE},              /* finish line, pause */
    {101, 0}, {129, 0}, {131, 0}, {133, 0}, {136, 0}, {138, 0}, {162, 0}, {164, 0},
    {169, 0}, {170, 0}, {380, 0}, {384, 0},
    {19, SONG | MKDS_MUSIC_ID(SEQ_FINAL_LAP)}, {10, SONG | MKDS_MUSIC_ID(SEQ_STAR)},
    {11, 0}, {12, 0}, {13, 0},
    {5, SONG | MKDS_MUSIC_ID(SEQ_GOAL_FIRST)}, {6, SONG | MKDS_MUSIC_ID(SEQ_GOAL_PLACED)},
    {7, SONG | MKDS_MUSIC_ID(SEQ_GOAL_LOST)},
    {33, SONG | MKDS_MUSIC_ID(SEQ_GOAL_FIRST)}, {34, SONG | MKDS_MUSIC_ID(SEQ_GOAL_LOST)},
};

/* Floor sounds by DS floor material (CSfxFloorMat order: asphalt, concrete,
 * edge, dirt, stone, sand, ice, grass, water, snow, board, carpet, wire
 * mesh, plastic, iron plate, rainbow, mud, stairs, M18), from the ARM9
 * table at 0x216ca64: slip (drift), chakuchi (landing) and run (driving).
 * 0xffff has no sound; run's board and stairs entries (0xffd0, 0xffd1) are
 * computed in code and left out. A floor's sound class is material + 1. */
static const uint16_t floor_sounds[3][MKDS_FLOOR_MATERIALS] = {
    {84, 85, 86, 87, 89, 90, 91, 92, 93, 101, 94, 95, 96, 97, 98, 99, 100, 0xffff, 0xffff},
    {103, 103, 103, 105, 103, 104, 106, 107, 111, 108, 103, 113, 109, 103, 103, 103, 110, 103, 103},
    {0xffff, 0xffff, 0xffff, 114, 117, 118, 0xffff, 115, 116, 121, 0xffff, 120, 0xffff, 0xffff, 0xffff, 0xffff, 119,
     0xffff, 0xffff},
};
static const uint16_t floor_cues[3] = {CUE_SURFACE_DRIFT, CUE_SURFACE_LAND, CUE_SURFACE_RUN};

/* Character voices for the set's events: boost and struck, then
 * PortableSoundVoiceEvent order. JOY (8) is the overtake guess; OT (1) and
 * the remote racers' OT3 and STARTDASH_OT have no event. */
static const uint8_t voice_events[2 + 10] = {
    VOICE_OT2, VOICE_SPIN,
    VOICE_STARTDASH, VOICE_OT1, VOICE_ATTACK, VOICE_JOY, VOICE_GOAL1, VOICE_GOAL23, VOICE_GOAL48,
    VOICE_MG_WIN, VOICE_MG_LOSE, VOICE_NONE,
};

/* SSAR 2 voice group of each DS character index (MKDS_DRIVER ds_id). */
static const uint8_t voice_group[13] = {4, 0, 1, 2, 5, 6, 7, 3, 10, 8, 9, 11, 12};

/* Positional object sounds (SSAR 0): course ambiances on the
 * AmbientSfxEmitter (MapObj 8) and BirdSfxEmitter (10), whose sound the game
 * picks in code we have not traced, so these follow the course's named
 * ambiance; and hazards, heard from where they are placed. Course "" matches
 * every course. */
static const MkdsObjectSound object_sounds[] = {
    {"town_course", 8, 407, 150, 0},     /* TOWN_AMBIANCE */
    {"mario_course", 8, 411, 150, 0},    /* MARIO_CROWD1 */
    {"stadium_course", 8, 443, 150, 0},  /* STADIUM_CROWD1 */
    {"airship_course", 8, 466, 150, 0},  /* AIRSHIP_FLY_AMBIANCE */
    {"pinball_course", 8, 460, 150, 0},  /* PINBALL_AMBIANCE */
    {"beach_course", 8, 274, 80, 0},     /* SEAWAVE */
    {"beach_course", 10, 273, 60, 0},    /* BEACHTORI */
    {"bank_course", 8, 265, 60, 0},      /* WATERFALL */
    {"mansion_course", 10, 291, 60, 0},  /* MORIFUKURO */
    {"", 207, 371, 40, 0},               /* Pendulum: FURIKO */
    {"", 316, 390, 40, 0},               /* Bakubaku */
    {"", 401, 386, 25, 0},               /* Goomba: KURIBO_WALK */
    {"", 406, 300, 50, 150},             /* Chain Chomp: WANWANBARK */
    {"", 415, 357, 30, 0},               /* Swoop: KOUMORI */
    {"", 417, 380, 60, 180},             /* Bullet Bill cannon: KILLER_SHOOT */
    {"", 418, 381, 60, 0},               /* Bullet Bill: KILLER_FLY */
    {"", 419, 287, 40, 0},               /* Move Tree: WALKINGTREE */
    {"", 420, 368, 30, 0},               /* MkdEfBurner: FLAME */
    {"", 421, 300, 50, 150},             /* Chain Chomp (Moving): WANWANBARK */
    {"", 426, 355, 30, 120},             /* Piranha Plant: FLOWERPAC_BITE */
    {"", 432, 461, 50, 0},               /* Iron Ball: IRONBALL_ROLL */
    {"", 434, 404, 30, 0},               /* Pokey: SANBO_WIGGLE */
};

const MkdsObjectSound *mkds_object_sound(const char *course, uint32_t object)
{
    for (size_t i = 0; i < sizeof object_sounds / sizeof object_sounds[0]; ++i)
        if (object_sounds[i].object == object && (!object_sounds[i].course[0] || !strcmp(object_sounds[i].course, course)))
            return &object_sounds[i];
    return NULL;
}

static uint16_t keep_effect(RiCtx *ctx, RiData sdat, uint8_t *kept, uint32_t archive, uint32_t effect)
{
    if (!kept[archive * 512u + effect]++)
        mkds_sound_effect(ctx, sdat, archive, effect, 4, MKDS_EFFECT_ID(archive, effect));
    return (uint16_t)(SONG | MKDS_EFFECT_ID(archive, effect));
}

void mkds_sound_set(RiCtx *ctx, RiData sdat, const uint32_t *driver_ids, const uint8_t *ds_ids, uint32_t drivers)
{
    for (uint32_t i = 0; i < sizeof event_sequences / sizeof event_sequences[0]; ++i)
        mkds_sound_sequence(ctx, sdat, event_sequences[i][0],
                            event_sequences[i][1] ? MKDS_MUSIC_ID(event_sequences[i][1]) : 0u);
    static uint8_t kept[3u * 512u];
    memset(kept, 0, sizeof kept);
    RiBuf *rows = ri_buf(ctx);
    uint32_t cue_count = 0;
    for (uint32_t i = 0; i < sizeof sound_cues / sizeof sound_cues[0]; ++i, ++cue_count)
    {
        const uint32_t song = sound_cues[i][1];
        ri_u16(ctx, rows, sound_cues[i][0]);
        ri_u16(ctx, rows, (uint16_t)(!song || song & SONG ? song : keep_effect(ctx, sdat, kept, 0, song)));
    }
    for (uint32_t kind = 0; kind < 3u; ++kind)
        for (uint32_t material = 0; material < MKDS_FLOOR_MATERIALS; ++material)
            if (floor_sounds[kind][material] != 0xffffu || kind == 0u)
            {
                const uint32_t effect = floor_sounds[kind][material];
                ri_u16(ctx, rows, (uint16_t)(floor_cues[kind] | (material + 1u)));
                ri_u16(ctx, rows, effect == 0xffffu ? 0u : keep_effect(ctx, sdat, kept, 0, effect));
                ++cue_count;
            }
    for (size_t i = 0; i < sizeof object_sounds / sizeof object_sounds[0]; ++i)
        keep_effect(ctx, sdat, kept, 0, object_sounds[i].effect);
    RiBuf *out = ri_buf(ctx);
    ri_put(ctx, out, "MKSCSND1", 8);
    ri_u16(ctx, out, 24);
    ri_u16(ctx, out, 3);
    ri_u16(ctx, out, (uint16_t)cue_count);
    ri_u16(ctx, out, (uint16_t)drivers);
    ri_u16(ctx, out, 22u * 256u); /* the engine rises 22 semitones to top speed */
    ri_u16(ctx, out, 0);
    ri_u16(ctx, out, FINAL_LAP_TEMPO);
    ri_u16(ctx, out, 0);
    ri_put_buf(ctx, out, rows);
    for (uint32_t i = 0; i < drivers; ++i)
    {
        if (ds_ids[i] >= sizeof(voice_group))
            ri_fail(ctx, "DS character %u has no voice group", ds_ids[i]);
        const uint32_t first = voice_group[ds_ids[i]] * VOICES_PER_CHARACTER;
        ri_u32(ctx, out, driver_ids[i]);
        for (uint32_t v = 0; v < sizeof voice_events; ++v)
        {
            ri_u16(ctx, out, voice_events[v] == VOICE_NONE ? 0u
                                                           : keep_effect(ctx, sdat, kept, 2, first + voice_events[v]));
            if (v == 1u)
            {
                ri_u16(ctx, out, 0); /* the set's engine cues */
                ri_u16(ctx, out, 0);
            }
        }
    }
    ri_section(ctx, MKDS_SOUND_SET_KIND, MKDS_SOUND_SET_ID, 3, out);
}
