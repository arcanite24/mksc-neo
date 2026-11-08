#include "rom_import_audio.h"

#include <stdlib.h>
#include <string.h>

/* Port of tools/portable/export_audio_sequence.py (TrackParser and
 * export_sequence) for ROM songs, where pointers are ROM addresses. */

enum {
    HEADER_SIZE = 32,
    STEP_BUDGET = 1000000,
    PATTERN_DEPTH = 3,
};

enum {
    OP_END = 1, OP_TEMPO, OP_PRIORITY, OP_KEY_SHIFT, OP_VOICE, OP_VOLUME, OP_PAN, OP_BEND,
    OP_BEND_RANGE, OP_LFO_SPEED, OP_LFO_DELAY, OP_MOD_DEPTH, OP_MOD_TYPE, OP_TUNE, OP_NOTE,
    OP_TIE, OP_END_TIE, OP_MEMORY_SET, OP_LOOP, OP_LOOP_POINT
};

/* PARAMETER_OPS, indexed by command - 0xBA; 0 marks commands not in the dict. */
static const uint32_t parameter_ops[0xC9 - 0xBA] = {
    OP_PRIORITY, OP_TEMPO, OP_KEY_SHIFT, OP_VOICE, OP_VOLUME, OP_PAN, OP_BEND, OP_BEND_RANGE,
    OP_LFO_SPEED, OP_LFO_DELAY, OP_MOD_DEPTH, OP_MOD_TYPE, 0, 0, OP_TUNE};

typedef struct Event {
    uint64_t tick;
    uint32_t track;
    uint32_t op;
    uint32_t value;
    uint32_t arg0;
    uint32_t arg1;
    uint32_t local;
    int has_target;
    uint32_t target_offset; /* while parsing */
    uint32_t target_local;  /* after parse() */
} Event;

/* One first-visited command offset: offset_events, offset_ticks and
 * emitted_offsets of the Python parser share the same key set. */
typedef struct OffsetEntry {
    uint32_t offset;
    uint32_t event_index;
    uint64_t tick;
    int emitted;
} OffsetEntry;

typedef struct TrackParser {
    RiCtx *ctx;
    uint32_t track;
    uint64_t tick;
    uint32_t running; /* 0 = None */
    uint32_t key;
    uint32_t velocity;
    uint32_t steps;
    RiBuf *events;  /* Event */
    RiBuf *entries; /* OffsetEntry */
    RiBuf *slots;   /* uint32_t open-addressing index: entry + 1, 0 empty */
} TrackParser;

static Event *events_of(TrackParser *parser)
{
    return (Event *)(void *)parser->events->data;
}

static uint32_t event_count(TrackParser *parser)
{
    return (uint32_t)(parser->events->size / sizeof(Event));
}

static OffsetEntry *entries_of(TrackParser *parser)
{
    return (OffsetEntry *)(void *)parser->entries->data;
}

static size_t entry_count(TrackParser *parser)
{
    return parser->entries->size / sizeof(OffsetEntry);
}

static size_t slot_count(TrackParser *parser)
{
    return parser->slots->size / sizeof(uint32_t);
}

static size_t slot_home(uint32_t offset, size_t mask)
{
    return (size_t)(offset * 2654435761u) & mask;
}

static void rehash(TrackParser *parser, size_t capacity)
{
    RiCtx *ctx = parser->ctx;
    ri_buf_free(ctx, parser->slots);
    parser->slots = ri_buf_zero(ctx, capacity * sizeof(uint32_t));
    uint32_t *slots = (uint32_t *)(void *)parser->slots->data;
    const OffsetEntry *entries = entries_of(parser);
    for (size_t index = 0; index < entry_count(parser); ++index) {
        size_t slot = slot_home(entries[index].offset, capacity - 1);
        while (slots[slot]) slot = (slot + 1) & (capacity - 1);
        slots[slot] = (uint32_t)(index + 1);
    }
}

/* Index of offset's entry, or -1. */
static int64_t find_offset(TrackParser *parser, uint32_t offset)
{
    const uint32_t *slots = (const uint32_t *)(const void *)parser->slots->data;
    size_t mask = slot_count(parser) - 1;
    const OffsetEntry *entries = entries_of(parser);
    for (size_t slot = slot_home(offset, mask); slots[slot]; slot = (slot + 1) & mask)
        if (entries[slots[slot] - 1].offset == offset) return (int64_t)slots[slot] - 1;
    return -1;
}

/* offset_events.setdefault(offset, len(events)); offset_ticks.setdefault(offset, tick) */
static void visit_offset(TrackParser *parser, uint32_t offset)
{
    if (find_offset(parser, offset) >= 0) return;
    OffsetEntry entry = {offset, event_count(parser), parser->tick, 0};
    ri_put(parser->ctx, parser->entries, &entry, sizeof entry);
    size_t capacity = slot_count(parser);
    if (entry_count(parser) * 2 > capacity) {
        rehash(parser, capacity * 2);
        return;
    }
    uint32_t *slots = (uint32_t *)(void *)parser->slots->data;
    size_t slot = slot_home(offset, capacity - 1);
    while (slots[slot]) slot = (slot + 1) & (capacity - 1);
    slots[slot] = (uint32_t)entry_count(parser);
}

static uint32_t read_byte(TrackParser *parser, uint32_t offset)
{
    if (offset >= parser->ctx->rom.size) ri_fail(parser->ctx, "track command exceeds sequence section");
    return parser->ctx->rom.data[offset];
}

/* TrackParser.emit */
static void emit(TrackParser *parser, uint32_t offset, uint32_t op, uint32_t value, uint32_t arg0,
                 uint32_t arg1, int has_target, uint32_t target)
{
    visit_offset(parser, offset);
    entries_of(parser)[find_offset(parser, offset)].emitted = 1;
    Event event = {parser->tick, parser->track, op, value, arg0, arg1, event_count(parser),
                   has_target, target, 0};
    ri_put(parser->ctx, parser->events, &event, sizeof event);
}

/* TrackParser._run */
static void run(TrackParser *parser, uint32_t offset, int pattern, const uint32_t *stack, size_t depth)
{
    RiCtx *ctx = parser->ctx;
    for (;;) {
        if (++parser->steps > STEP_BUDGET) ri_fail(ctx, "sequence expansion exceeds step budget");
        uint32_t command_offset = offset;
        visit_offset(parser, command_offset);
        uint32_t command = read_byte(parser, offset);
        if (command < 0x80) {
            if (!parser->running) ri_fail(ctx, "missing running status at 0x%x", (unsigned)offset);
            command = parser->running;
        } else {
            offset += 1;
            if (command >= 0xBD) parser->running = command;
        }
        if (command >= 0x80 && command <= 0xB0) {
            parser->tick += ri_get_u8(parser->ctx,parser->ctx->rom,0xf3444u+command-0x80u);
            continue;
        }
        if (command == 0xB1) { /* FINE */
            if (pattern) ri_fail(ctx, "FINE inside pattern");
            emit(parser, command_offset, OP_END, 0, 0, 0, 0, 0);
            return;
        }
        if (command == 0xB2) { /* GOTO */
            uint32_t target = (uint32_t)ri_pointer(ctx, ctx->rom, offset);
            offset += 4;
            if (target >= command_offset) ri_fail(ctx, "only bounded backward GOTO is supported");
            emit(parser, command_offset, OP_LOOP, 0, 0, 0, 1, target);
            return;
        }
        if (command == 0xB3) { /* PATT */
            uint32_t target = (uint32_t)ri_pointer(ctx, ctx->rom, offset);
            offset += 4;
            int recursive = depth >= PATTERN_DEPTH;
            for (size_t index = 0; index < depth; ++index)
                recursive |= stack[index] == target;
            if (recursive) ri_fail(ctx, "recursive or over-depth pattern");
            uint32_t inner[PATTERN_DEPTH];
            if (depth) memcpy(inner, stack, depth * sizeof(*stack));
            inner[depth] = target;
            run(parser, target, 1, inner, depth + 1);
            continue;
        }
        if (command == 0xB4) { /* PEND */
            if (pattern) return;
            continue;
        }
        if (command == 0xB5) ri_fail(ctx, "REPT is not used by the V03 inventory");
        if (command == 0xB9) { /* MEMACC */
            const uint8_t *bytes = ri_at(ctx, ctx->rom, offset, 3);
            offset += 3;
            if (bytes[0] != 0) ri_fail(ctx, "only MEMACC set is used by the V03 inventory");
            emit(parser, command_offset, OP_MEMORY_SET, 0, bytes[1], bytes[2], 0, 0);
            continue;
        }
        if (command >= 0xBA && command < 0xC9 && parameter_ops[command - 0xBA]) {
            uint32_t value = read_byte(parser, offset);
            if (value >= 0x80) ri_fail(ctx, "missing parameter at 0x%x", (unsigned)command_offset);
            offset += 1;
            emit(parser, command_offset, parameter_ops[command - 0xBA], value, 0, 0, 0, 0);
            continue;
        }
        if (command == 0xCE) { /* EOT */
            uint32_t key = parser->key;
            if (read_byte(parser, offset) < 0x80) {
                key = read_byte(parser, offset);
                offset += 1;
            }
            emit(parser, command_offset, OP_END_TIE, 0, key, 0, 0, 0);
            continue;
        }
        if (command >= 0xCF && command <= 0xFF) { /* TIE and N01..N96 */
            uint32_t key = parser->key, velocity = parser->velocity;
            if (read_byte(parser, offset) < 0x80) {
                key = read_byte(parser, offset);
                offset += 1;
                if (read_byte(parser, offset) < 0x80) {
                    velocity = read_byte(parser, offset);
                    offset += 1;
                }
            }
            uint32_t gate = command == 0xCF ? 0 : ri_get_u8(parser->ctx,parser->ctx->rom,0xf3444u+command-0xcfu);
            if (read_byte(parser, offset) < 0x80) {
                gate += read_byte(parser, offset);
                offset += 1;
            }
            if (gate > 255) ri_fail(ctx, "note gate exceeds normalized field");
            parser->key = key;
            parser->velocity = velocity;
            emit(parser, command_offset, command == 0xCF ? OP_TIE : OP_NOTE, gate, key, velocity, 0, 0);
            continue;
        }
        ri_fail(ctx, "unsupported command 0x%02x at 0x%x", (unsigned)command, (unsigned)command_offset);
    }
}

static OffsetEntry *target_entry(TrackParser *parser, uint32_t target)
{
    int64_t index = find_offset(parser, target);
    if (index < 0) ri_fail(parser->ctx, "loop target 0x%x is not an event boundary", (unsigned)target);
    return &entries_of(parser)[index];
}

/* TrackParser.parse: returns the track's events with target_local resolved. */
static void parse(TrackParser *parser, uint32_t start)
{
    RiCtx *ctx = parser->ctx;
    run(parser, start, 0, NULL, 0);
    uint32_t count = event_count(parser);
    if (!count || (events_of(parser)[count - 1].op != OP_END && events_of(parser)[count - 1].op != OP_LOOP))
        ri_fail(ctx, "track %u did not terminate", (unsigned)parser->track);

    /* targets = {...}; sorted(targets, key=offset_events.__getitem__). The
     * key is read before any insertion. Python iterates a set here, so two
     * non-emitted targets with the same key would be ordered by CPython's set
     * layout; the US ROM has at most one target per track, and such a tie is
     * refused rather than guessed. */
    RiBuf *target_buf = ri_buf(ctx);
    for (uint32_t index = 0; index < count; ++index) {
        const Event *event = &events_of(parser)[index];
        if (!event->has_target) continue;
        const uint32_t *targets = (const uint32_t *)(const void *)target_buf->data;
        size_t known = target_buf->size / sizeof(uint32_t), seen = 0;
        while (seen < known && targets[seen] != event->target_offset) ++seen;
        if (seen == known) ri_put(ctx, target_buf, &event->target_offset, sizeof(uint32_t));
    }
    uint32_t *targets = (uint32_t *)(void *)target_buf->data;
    size_t target_count = target_buf->size / sizeof(uint32_t);
    RiBuf *key_buf = ri_buf_zero(ctx, target_count * sizeof(uint32_t));
    uint32_t *keys = (uint32_t *)(void *)key_buf->data;
    for (size_t index = 0; index < target_count; ++index) {
        const OffsetEntry *entry = target_entry(parser, targets[index]);
        keys[index] = entry->event_index;
        for (size_t other = 0; other < index; ++other)
            if (keys[other] == keys[index] && !entry->emitted &&
                !target_entry(parser, targets[other])->emitted)
                ri_fail(ctx, "ambiguous loop target order in track %u", (unsigned)parser->track);
    }
    for (size_t index = 1; index < target_count; ++index) /* stable insertion sort */
        for (size_t at = index; at > 0 && keys[at - 1] > keys[at]; --at) {
            uint32_t key = keys[at], target = targets[at];
            keys[at] = keys[at - 1];
            targets[at] = targets[at - 1];
            keys[at - 1] = key;
            targets[at - 1] = target;
        }

    for (size_t index = 0; index < target_count; ++index) {
        OffsetEntry *entry = target_entry(parser, targets[index]);
        if (entry->emitted) continue;
        uint32_t boundary = entry->event_index;
        Event point = {entry->tick, parser->track, OP_LOOP_POINT, 0, 0, 0, boundary, 0, 0, 0};
        size_t tail = parser->events->size - (size_t)boundary * sizeof(Event);
        ri_resize(ctx, parser->events, parser->events->size + sizeof(Event));
        Event *events = events_of(parser);
        memmove(&events[boundary + 1], &events[boundary], tail);
        events[boundary] = point;
        OffsetEntry *entries = entries_of(parser);
        for (size_t other = 0; other < entry_count(parser); ++other)
            if (entries[other].event_index >= boundary) entries[other].event_index += 1;
        entry->event_index = boundary;
        entry->emitted = 1;
    }
    Event *events = events_of(parser);
    count = event_count(parser);
    for (uint32_t local = 0; local < count; ++local)
        events[local].local = local;
    for (uint32_t local = 0; local < count; ++local)
        if (events[local].has_target)
            events[local].target_local = target_entry(parser, events[local].target_offset)->event_index;
    ri_buf_free(ctx, key_buf);
    ri_buf_free(ctx, target_buf);
}

/* key=lambda event: (event["tick"], event["track"], event["local"]) */
static int event_order(const void *left, const void *right)
{
    const Event *a = left, *b = right;
    if (a->tick != b->tick) return a->tick < b->tick ? -1 : 1;
    if (a->track != b->track) return a->track < b->track ? -1 : 1;
    if (a->local != b->local) return a->local < b->local ? -1 : 1;
    return 0;
}

static void put_u16_checked(RiCtx *ctx, RiBuf *out, uint64_t value)
{
    if (value > 0xFFFFu) ri_fail(ctx, "sequence field outside uint16");
    ri_u16(ctx, out, (uint32_t)value);
}

static void put_u32_checked(RiCtx *ctx, RiBuf *out, uint64_t value)
{
    if (value > 0xFFFFFFFFu) ri_fail(ctx, "sequence field outside uint32");
    ri_u32(ctx, out, (uint32_t)value);
}

static void put_u8_checked(RiCtx *ctx, RiBuf *out, uint32_t value)
{
    if (value > 0xFFu) ri_fail(ctx, "sequence field outside uint8");
    ri_u8(ctx, out, value);
}

RiBuf *ri_audio_sequence(RiCtx *ctx, size_t header, uint32_t song_id, uint32_t bank_id)
{
    const uint8_t *head = ri_at(ctx, ctx->rom, header, 8); /* "song header exceeds section" */
    uint32_t track_count = head[0], block_count = head[1], priority = head[2], reverb = head[3];
    if (track_count < 1 || track_count > 16 || block_count != 0) ri_fail(ctx, "unsupported song header");

    TrackParser parsers[16];
    size_t total_events = 0;
    for (uint32_t track = 0; track < track_count; ++track) {
        uint32_t start = (uint32_t)ri_pointer(ctx, ctx->rom, header + 8 + track * 4);
        TrackParser *parser = &parsers[track];
        memset(parser, 0, sizeof *parser);
        parser->ctx = ctx;
        parser->track = track;
        parser->events = ri_buf(ctx);
        parser->entries = ri_buf(ctx);
        parser->slots = ri_buf_zero(ctx, 256 * sizeof(uint32_t));
        parse(parser, start);
        total_events += event_count(parser);
    }

    RiBuf *merged_buf = ri_buf(ctx);
    for (uint32_t track = 0; track < track_count; ++track)
        ri_put_buf(ctx, merged_buf, parsers[track].events);
    Event *merged = (Event *)(void *)merged_buf->data;
    qsort(merged, total_events, sizeof(Event), event_order);
    /* global_indices[(track, local)], stored back into each track's events. */
    for (size_t index = 0; index < total_events; ++index)
        events_of(&parsers[merged[index].track])[merged[index].local].local = (uint32_t)index;

    RiBuf *out = ri_buf(ctx);
    ri_put(ctx, out, "MKSCAUD1", 8);
    ri_u16(ctx, out, HEADER_SIZE);
    ri_u16(ctx, out, 2);
    put_u16_checked(ctx, out, song_id);
    ri_u8(ctx, out, track_count);
    ri_u8(ctx, out, priority);
    ri_u8(ctx, out, reverb);
    const uint32_t player=ri_get_u16(ctx,ctx->rom,0x102498u+(size_t)song_id*8u+4u);
    if(player>=11u)ri_fail(ctx,"invalid sequence player");
    ri_u8(ctx, out, player);
    put_u16_checked(ctx, out, bank_id);
    put_u32_checked(ctx, out, total_events);
    ri_u32(ctx, out, HEADER_SIZE);
    put_u32_checked(ctx, out, HEADER_SIZE + (uint64_t)total_events * 16);
    for (size_t index = 0; index < total_events; ++index) {
        const Event *event = &merged[index];
        uint32_t target = 0;
        if (event->op == OP_LOOP) target = events_of(&parsers[event->track])[event->target_local].local;
        put_u32_checked(ctx, out, event->tick);
        ri_u32(ctx, out, target);
        put_u16_checked(ctx, out, event->value);
        ri_u8(ctx, out, event->track);
        ri_u8(ctx, out, event->op);
        put_u8_checked(ctx, out, event->arg0);
        put_u8_checked(ctx, out, event->arg1);
        ri_u16(ctx, out, 0);
    }
    for (uint32_t track = 0; track < track_count; ++track) {
        ri_buf_free(ctx, parsers[track].events);
        ri_buf_free(ctx, parsers[track].entries);
        ri_buf_free(ctx, parsers[track].slots);
    }
    ri_buf_free(ctx, merged_buf);
    return out;
}
