#include "rom_import_audio.h"

#include <string.h>

/* Port of tools/portable/export_audio_voice_bank.py BankFlattener as
 * specialized by audio_rom.py RomBankFlattener: ToneData tables are read from
 * ROM addresses, and samples are numbered through RomAudio.sample_id. */

enum { HEADER_SIZE = 40, MAX_RESOLVE_DEPTH = 8, KEY_COUNT = 128 };

/* The tuple (tone_type, key, length, pan, attack, decay, sustain, release,
 * kind, source_id, first, mapping_count). Native scratch is compared by value
 * and emitted through explicit little-endian writers. */
typedef struct Voice {
    uint32_t tone_type, key, length, pan;
    uint32_t attack, decay, sustain, release;
    uint32_t kind, source_id, first, mapping_count;
} Voice;

typedef struct CacheEntry {
    Voice voice;
    uint32_t index;
} CacheEntry;

/* [start, end, target, tone_key, pan] */
typedef struct Mapping {
    uint32_t start, end, target, tone_key, pan;
} Mapping;

typedef struct Identity {
    uint32_t address, index, key;
} Identity;

typedef struct Flattener {
    RiAudio *audio;
    RiCtx *ctx;
    RiBuf *voices;   /* Voice: output_voices */
    RiBuf *cache;    /* CacheEntry: direct_cache */
    RiBuf *mappings; /* Mapping */
} Flattener;

static Voice *voices_of(Flattener *flattener)
{
    return (Voice *)(void *)flattener->voices->data;
}

static size_t voice_count(const Flattener *flattener)
{
    return flattener->voices->size / sizeof(Voice);
}

static size_t mapping_count(const Flattener *flattener)
{
    return flattener->mappings->size / sizeof(Mapping);
}

/* RomBankFlattener.raw_tone: ROM offset of the entry, or -1 for UnmappedKey. */
static int64_t raw_tone(Flattener *flattener, uint32_t address, uint32_t index)
{
    if ((int64_t)index >= ri_audio_tone_count(flattener->audio, address)) return -1;
    int64_t offset = (int64_t)address + (int64_t)index * 12;
    ri_at(flattener->ctx, flattener->ctx->rom, (size_t)offset, 12); /* RomAudio.read */
    return offset;
}

/* BankFlattener.direct for the entry at ROM offset (already range checked). */
static Voice direct(Flattener *flattener, size_t offset)
{
    RiCtx *ctx = flattener->ctx;
    const uint8_t *raw = ri_at(ctx, ctx->rom, offset, 12);
    Voice voice = {raw[0], raw[1], raw[2], raw[3], raw[8], raw[9], raw[10], raw[11], 0, 0, 0, 0};
    uint32_t channel = voice.tone_type & 7;
    if (voice.tone_type & 0xC0) ri_fail(ctx, "composite voice reached direct decoder");
    if (channel == 0) {
        voice.kind = RI_VOICE_PCM;
        voice.source_id = ri_audio_sample_id(flattener->audio, voice.kind,
                                             (uint32_t)ri_pointer(ctx, ctx->rom, offset + 4));
    } else if (channel == 1 || channel == 2) {
        voice.kind = RI_VOICE_PSG_SQUARE;
        voice.source_id = ri_le32(raw + 4);
    } else if (channel == 3) {
        voice.kind = RI_VOICE_PSG_WAVE;
        voice.source_id = ri_audio_sample_id(flattener->audio, voice.kind,
                                             (uint32_t)ri_pointer(ctx, ctx->rom, offset + 4));
    } else if (channel == 4) {
        voice.kind = RI_VOICE_PSG_NOISE;
        voice.source_id = ri_le32(raw + 4);
    } else {
        ri_fail(ctx, "unsupported voice type 0x%02x", (unsigned)voice.tone_type);
    }
    if (voice.key > 127 || (voice.kind == RI_VOICE_PSG_SQUARE && voice.source_id > 3) ||
        (voice.kind == RI_VOICE_PSG_NOISE && voice.source_id > 1))
        ri_fail(ctx, "invalid direct voice parameter");
    return voice;
}

/* BankFlattener.resolve: 1 with *out set, or 0 for UnmappedKey. */
static int resolve(Flattener *flattener, uint32_t address, uint32_t index, uint32_t key,
                   const Identity *stack, size_t depth, Voice *out)
{
    RiCtx *ctx = flattener->ctx;
    Identity identity = {address, index, key};
    int recursive = depth >= MAX_RESOLVE_DEPTH;
    for (size_t at = 0; at < depth; ++at)
        recursive |= stack[at].address == address && stack[at].index == index && stack[at].key == key;
    if (recursive) ri_fail(ctx, "recursive voice table");
    int64_t offset = raw_tone(flattener, address, index);
    if (offset < 0) return 0;
    uint32_t tone_type = ri_get_u8(ctx, ctx->rom, (size_t)offset);
    if (!(tone_type & 0xC0)) {
        *out = direct(flattener, (size_t)offset);
        return 1;
    }
    uint32_t child = (uint32_t)ri_pointer(ctx, ctx->rom, (size_t)offset + 4);
    Identity inner[MAX_RESOLVE_DEPTH];
    if (depth) memcpy(inner, stack, depth * sizeof(*stack));
    inner[depth] = identity;
    if (tone_type & 0x80) return resolve(flattener, child, key, key, inner, depth + 1, out);
    uint32_t mapping = (uint32_t)ri_pointer(ctx, ctx->rom, (size_t)offset + 8);
    /* RomBankFlattener.mapping_key: rom.read(address + addend + key, 1)[0] */
    uint32_t mapped = ri_get_u8(ctx, ctx->rom, (size_t)mapping + key);
    return resolve(flattener, child, mapped, key, inner, depth + 1, out);
}

static int voices_equal(const Voice *left, const Voice *right)
{
    return left->tone_type == right->tone_type && left->key == right->key &&
           left->length == right->length && left->pan == right->pan &&
           left->attack == right->attack && left->decay == right->decay &&
           left->sustain == right->sustain && left->release == right->release &&
           left->kind == right->kind && left->source_id == right->source_id &&
           left->first == right->first && left->mapping_count == right->mapping_count;
}

/* direct_cache.setdefault(voice, index); returns the cached index. */
static uint32_t cache_setdefault(Flattener *flattener, const Voice *voice, uint32_t index)
{
    const CacheEntry *entries = (const CacheEntry *)(const void *)flattener->cache->data;
    size_t count = flattener->cache->size / sizeof(CacheEntry);
    for (size_t at = 0; at < count; ++at)
        if (voices_equal(&entries[at].voice, voice)) return entries[at].index;
    CacheEntry entry = {*voice, index};
    ri_put(flattener->ctx, flattener->cache, &entry, sizeof entry);
    return index;
}

/* BankFlattener.append_direct */
static uint32_t append_direct(Flattener *flattener, const Voice *voice)
{
    uint32_t index = (uint32_t)voice_count(flattener);
    uint32_t cached = cache_setdefault(flattener, voice, index);
    if (cached == index) ri_put(flattener->ctx, flattener->voices, voice, sizeof *voice);
    return cached;
}

/* BankFlattener.composite: sets *first and *count of the appended mappings. */
static void composite(Flattener *flattener, uint32_t address, uint32_t index, int is_drum,
                      uint32_t *first, uint32_t *count)
{
    Mapping groups[KEY_COUNT];
    size_t group_count = 0;
    for (uint32_t key = 0; key < KEY_COUNT; ++key) {
        Voice voice;
        if (!resolve(flattener, address, index, key, NULL, 0, &voice)) continue;
        uint32_t target = append_direct(flattener, &voice);
        Mapping *last = group_count ? &groups[group_count - 1] : NULL;
        if (!is_drum && last && last->target == target && last->tone_key == voice.key &&
            last->pan == voice.pan && last->end + 1 == key) {
            last->end = key;
        } else {
            Mapping group = {key, key, target, voice.key, voice.pan};
            groups[group_count++] = group;
        }
    }
    if (!group_count) ri_fail(flattener->ctx, "composite voice has no mapped keys");
    *first = (uint32_t)mapping_count(flattener);
    *count = (uint32_t)group_count;
    ri_put(flattener->ctx, flattener->mappings, groups, group_count * sizeof(Mapping));
}

static void put_u16_checked(RiCtx *ctx, RiBuf *out, uint64_t value)
{
    if (value > 0xFFFFu) ri_fail(ctx, "voice bank field outside uint16");
    ri_u16(ctx, out, (uint32_t)value);
}

static void put_u32_checked(RiCtx *ctx, RiBuf *out, uint64_t value)
{
    if (value > 0xFFFFFFFFu) ri_fail(ctx, "voice bank field outside uint32");
    ri_u32(ctx, out, (uint32_t)value);
}

/* BankFlattener.export(symbol=address, bank_id) */
RiBuf *ri_audio_bank(RiAudio *audio, uint32_t address, uint32_t bank_id)
{
    RiCtx *ctx = audio->ctx;
    Flattener flattener = {audio, ctx, ri_buf(ctx), ri_buf(ctx), ri_buf(ctx)};
    int64_t count = ri_audio_tone_count(audio, address);
    if (count < 1 || count > 128) ri_fail(ctx, "bank at 0x%x: top-level voice bank must contain 1..128 voices",
                                          (unsigned)address);
    /* Reserve original indices before appending flattened child voices. */
    ri_resize(ctx, flattener.voices, (size_t)count * sizeof(Voice));
    for (uint32_t index = 0; index < (uint32_t)count; ++index) {
        int64_t offset = raw_tone(&flattener, address, index);
        if (offset < 0) ri_fail(ctx, "bank at 0x%x: unmapped key", (unsigned)address);
        const uint8_t *raw = ri_at(ctx, ctx->rom, (size_t)offset, 12);
        Voice voice = {raw[0], raw[1], raw[2], raw[3], 0, 0, 0, 0, 0, 0, 0, 0};
        if (voice.tone_type & 0xC0) {
            int is_drum = (voice.tone_type & 0x80) != 0;
            voice.kind = is_drum ? RI_VOICE_DRUM : RI_VOICE_KEY_SPLIT;
            composite(&flattener, address, index, is_drum, &voice.first, &voice.mapping_count);
            voices_of(&flattener)[index] = voice;
        } else {
            voice = direct(&flattener, (size_t)offset);
            voices_of(&flattener)[index] = voice;
            cache_setdefault(&flattener, &voice, index);
        }
    }

    size_t voices = voice_count(&flattener), mappings = mapping_count(&flattener);
    uint64_t mappings_offset = HEADER_SIZE + (uint64_t)voices * 20;
    RiBuf *out = ri_buf(ctx);
    ri_put(ctx, out, "MKSCVOC1", 8);
    ri_u16(ctx, out, HEADER_SIZE);
    ri_u16(ctx, out, 1);
    put_u16_checked(ctx, out, bank_id);
    put_u16_checked(ctx, out, voices);
    put_u32_checked(ctx, out, mappings);
    ri_u32(ctx, out, HEADER_SIZE);
    put_u32_checked(ctx, out, mappings_offset);
    put_u32_checked(ctx, out, mappings_offset + (uint64_t)mappings * 8);
    ri_u32(ctx, out, 0);
    ri_u32(ctx, out, 0);
    for (size_t index = 0; index < voices; ++index) { /* "<BBBBBBBBB3xIHH" */
        const Voice *voice = &voices_of(&flattener)[index];
        const uint32_t bytes[9] = {voice->tone_type, voice->key, voice->length, voice->pan, voice->attack,
                                   voice->decay, voice->sustain, voice->release, voice->kind};
        for (int field = 0; field < 9; ++field)
            ri_u8(ctx, out, bytes[field]);
        ri_put_zero(ctx, out, 3);
        ri_u32(ctx, out, voice->source_id);
        put_u16_checked(ctx, out, voice->first);
        put_u16_checked(ctx, out, voice->mapping_count);
    }
    const Mapping *items = (const Mapping *)(const void *)flattener.mappings->data;
    for (size_t index = 0; index < mappings; ++index) { /* "<BBHBBH" */
        ri_u8(ctx, out, items[index].start);
        ri_u8(ctx, out, items[index].end);
        put_u16_checked(ctx, out, items[index].target);
        ri_u8(ctx, out, items[index].tone_key);
        ri_u8(ctx, out, items[index].pan);
        ri_u16(ctx, out, 0);
    }
    ri_buf_free(ctx, flattener.voices);
    ri_buf_free(ctx, flattener.cache);
    ri_buf_free(ctx, flattener.mappings);
    return out;
}
