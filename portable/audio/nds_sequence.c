#include "nds_sequence.h"

#include <string.h>

/* Envelope amplitude is in decibel tenths << 7; this is silence. */
#define AMPLITUDE_FLOOR (-92544)
/* The DS sound driver updates every 64 * 2728 ARM7 cycles (~192 Hz). */
#define ARM7_CLOCK UINT64_C(33513982)
#define FRAME_CYCLES UINT64_C(174592)
#define CHANNEL_CLOCK UINT64_C(16756991)
#define COMMAND_LIMIT 4096u

enum { ATTACK = 0, DECAY, SUSTAIN, RELEASE };
enum { ARG_U8 = 0, ARG_S8, ARG_U16, ARG_S16, ARG_VARIABLE_LENGTH };
enum { REGION_PCM = 1, REGION_PSG = 2, REGION_NOISE = 3 };

/* 200 log10(i / 127): decibel tenths for 7-bit volumes. */
static const int16_t decibels[128] = {
    -32768, -421, -361, -325, -300, -281, -265, -252, -240, -230, -221, -212, -205, -198, -192, -186,
    -180, -175, -170, -165, -161, -156, -152, -148, -145, -141, -138, -134, -131, -128, -125, -122,
    -120, -117, -114, -112, -110, -107, -105, -103, -100, -98, -96, -94, -92, -90, -88, -86,
    -85, -83, -81, -79, -78, -76, -74, -73, -71, -70, -68, -67, -65, -64, -62, -61,
    -60, -58, -57, -56, -54, -53, -52, -51, -49, -48, -47, -46, -45, -43, -42, -41,
    -40, -39, -38, -37, -36, -35, -34, -33, -32, -31, -30, -29, -28, -27, -26, -25,
    -24, -23, -23, -22, -21, -20, -19, -18, -17, -17, -16, -15, -14, -13, -13, -12,
    -11, -10, -10, -9, -8, -7, -7, -6, -5, -4, -4, -3, -2, -1, -1, 0};
/* 127 sin(i pi / 64): the LFO's quarter wave. */
static const uint8_t quarter_sine[33] = {0, 6, 12, 19, 25, 31, 37, 43, 49, 54, 60, 65, 71, 76, 81, 85, 90,
                                         94, 98, 102, 106, 109, 112, 115, 117, 120, 122, 123, 125, 126, 126, 127, 127};
static const int8_t adpcm_index[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
static const uint16_t adpcm_step[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724,
    796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026,
    4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500,
    20350, 22385, 24623, 27086, 29794, 32767};

static uint16_t u16le(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t u32le(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static int32_t clamp(int32_t v, int32_t low, int32_t high) { return v < low ? low : v > high ? high : v; }
static int16_t sustain_decibels(uint8_t value) { return decibels[value & 0x7fu]; }

static int span(uint32_t size, uint32_t offset, uint64_t length)
{
    return offset <= size && length <= size - offset;
}

PortableNdsStatus portable_nds_sequence_open(PortableNdsSequenceView *view, const uint8_t *bytes, size_t size)
{
    if (!view || !bytes)
        return PORTABLE_NDS_INVALID_ARGUMENT;
    memset(view, 0, sizeof *view);
    if (size < PORTABLE_NDS_SEQUENCE_HEADER_SIZE || size > UINT32_MAX || memcmp(bytes, "MKSCNSQ1", 8))
        return PORTABLE_NDS_BAD_SECTION;
    const unsigned version = u16le(bytes + 10);
    const uint32_t header = version == 2u ? PORTABLE_NDS_SEQUENCE_HEADER_SIZE_V2 : PORTABLE_NDS_SEQUENCE_HEADER_SIZE;
    if ((version != 1u && version != 2u) || u16le(bytes + 8) != header || size < header || bytes[12] > 127u ||
        u16le(bytes + 14) > 0x7fffu)
        return PORTABLE_NDS_BAD_SECTION;
    PortableNdsSequenceView v = {bytes, (uint32_t)size, bytes[12], bytes[13], (uint16_t)u16le(bytes + 14),
                                 u32le(bytes + 16), u32le(bytes + 20), u32le(bytes + 24), u32le(bytes + 28),
                                 u32le(bytes + 32), u32le(bytes + 36), u32le(bytes + 40), u32le(bytes + 44),
                                 version == 2u ? u32le(bytes + 48) : 0u};
    if (!v.sequence_size || v.start >= v.sequence_size || !span(v.size, v.sequence_offset, v.sequence_size) ||
        !v.program_count ||
        v.program_count > 65536u || !span(v.size, v.program_offset, (uint64_t)v.program_count * 4u) ||
        v.region_count > 65535u || !span(v.size, v.region_offset, (uint64_t)v.region_count * 12u) ||
        v.wave_count > 65535u || !span(v.size, v.wave_offset, (uint64_t)v.wave_count * 24u))
        return PORTABLE_NDS_BAD_SECTION;
    for (uint32_t i = 0; i < v.program_count; ++i)
    {
        const uint8_t *p = bytes + v.program_offset + i * 4u;
        if ((uint32_t)u16le(p) + u16le(p + 2) > v.region_count)
            return PORTABLE_NDS_BAD_SECTION;
    }
    for (uint32_t i = 0; i < v.region_count; ++i)
    {
        const uint8_t *r = bytes + v.region_offset + i * 12u;
        if (r[0] > r[1] || r[1] > 127u || r[2] < REGION_PCM || r[2] > REGION_NOISE || r[3] > 127u || r[9] ||
            (r[2] == REGION_PCM && u16le(r + 10) >= v.wave_count) || (r[2] == REGION_PSG && u16le(r + 10) > 6u))
            return PORTABLE_NDS_BAD_SECTION;
    }
    for (uint32_t i = 0; i < v.wave_count; ++i)
    {
        const uint8_t *w = bytes + v.wave_offset + i * 24u;
        const uint32_t loop_start = u32le(w + 8), count = u32le(w + 12), data = u32le(w + 16), data_size = u32le(w + 20);
        const uint64_t need = w[0] == 0u ? count : w[0] == 1u ? (uint64_t)count * 2u : 4u + ((uint64_t)count + 1u) / 2u;
        if (w[0] > 2u || w[1] > 1u || u16le(w + 2) || !u16le(w + 4) || u16le(w + 6) || !count ||
            (w[1] && loop_start >= count) || need > data_size || !span(v.size, data, data_size))
            return PORTABLE_NDS_BAD_SECTION;
    }
    *view = v;
    return PORTABLE_NDS_OK;
}

/* 2^(x / 65536) in Q16 (x Q16 log2 units, |x| < 32 << 16). */
static uint64_t exp2_q16(int32_t x)
{
    const int32_t whole = x >= 0 ? x >> 16 : -((-x + 65535) >> 16);
    const uint64_t f = (uint64_t)(uint32_t)(x - whole * 65536);
    /* 2^f ~ 1 + 0.6951 f + 0.2262 f^2 + 0.0782 f^3 on [0, 1). */
    const uint64_t fraction = 65536u + ((f * (45554u + ((f * (14824u + ((f * 5125u) >> 16))) >> 16))) >> 16);
    return whole >= 0 ? fraction << whole : fraction >> -whole;
}

/* Linear Q16 gain of a level in decibel tenths (-723..0). */
static int32_t decibel_gain(int32_t tenths)
{
    return tenths <= -723 ? 0 : (int32_t)exp2_q16((int32_t)((int64_t)tenths * 108853 / 100));
}

static uint8_t attack_rate(uint8_t attack)
{
    static const uint8_t slow[19] = {0x00, 0x01, 0x05, 0x0e, 0x1a, 0x26, 0x33, 0x3f, 0x49, 0x54,
                                     0x5c, 0x64, 0x6d, 0x74, 0x7b, 0x7f, 0x84, 0x89, 0x8f};
    attack &= 0x7fu;
    return attack >= 0x6du ? slow[0x7fu - attack] : (uint8_t)(0xffu - attack);
}

static uint16_t fall_rate(uint8_t fall)
{
    fall &= 0x7fu;
    if (fall == 0x7fu)
        return 0xffffu;
    if (fall == 0x7eu)
        return 0x3c00u;
    if (fall < 0x32u)
        return (uint16_t)(fall * 2u + 1u);
    return (uint16_t)(0x1e00u / (0x7eu - fall));
}

static PortableNdsStatus fail(PortableNdsPlayer *p, PortableNdsStatus status, uint32_t at)
{
    p->status = status;
    p->failed_at = at;
    p->active = 0;
    return status;
}

static uint16_t next_random(PortableNdsPlayer *p)
{
    p->random = p->random * 1664525u + 1013904223u;
    return (uint16_t)(p->random >> 16);
}

/* Sequence reading: every read is bounds-checked against the song. */
static int read_byte(PortableNdsPlayer *p, PortableNdsTrack *t, uint8_t *out)
{
    if (t->position >= p->song.sequence_size)
        return 0;
    *out = p->song.bytes[p->song.sequence_offset + t->position++];
    return 1;
}

static int read_fixed(PortableNdsPlayer *p, PortableNdsTrack *t, unsigned bytes, uint32_t *out)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < bytes; ++i)
    {
        uint8_t b;
        if (!read_byte(p, t, &b))
            return 0;
        value |= (uint32_t)b << (8u * i);
    }
    *out = value;
    return 1;
}

static int read_variable_length(PortableNdsPlayer *p, PortableNdsTrack *t, int32_t *out)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < 4u; ++i)
    {
        uint8_t b;
        if (!read_byte(p, t, &b))
            return 0;
        value = value << 7 | (b & 0x7fu);
        if (!(b & 0x80u))
        {
            *out = (int32_t)value;
            return 1;
        }
    }
    return 0;
}

/* Reads a command's last argument, honouring the random (0xA0) and
 * variable (0xA1) prefixes. */
static int read_argument(PortableNdsPlayer *p, PortableNdsTrack *t, unsigned type, unsigned prefix, int32_t *out)
{
    uint32_t raw;
    if (prefix == 0xa0u)
    {
        uint32_t low, high;
        if (!read_fixed(p, t, 2, &low) || !read_fixed(p, t, 2, &high))
            return 0;
        const int32_t a = (int16_t)low, b = (int16_t)high;
        *out = b >= a ? a + (int32_t)(next_random(p) % (uint32_t)(b - a + 1)) : a;
        return 1;
    }
    if (prefix == 0xa1u)
    {
        if (!read_fixed(p, t, 1, &raw) || raw >= 32u)
            return 0;
        *out = p->variables[raw];
        return 1;
    }
    switch (type)
    {
    case ARG_U8:
        if (!read_fixed(p, t, 1, &raw))
            return 0;
        *out = (int32_t)raw;
        return 1;
    case ARG_S8:
        if (!read_fixed(p, t, 1, &raw))
            return 0;
        *out = (int8_t)raw;
        return 1;
    case ARG_U16:
        if (!read_fixed(p, t, 2, &raw))
            return 0;
        *out = (int32_t)raw;
        return 1;
    case ARG_S16:
        if (!read_fixed(p, t, 2, &raw))
            return 0;
        *out = (int16_t)raw;
        return 1;
    default:
        return read_variable_length(p, t, out);
    }
}

static void reset_track(PortableNdsTrack *t, uint32_t position)
{
    *t = (PortableNdsTrack){.position = position, .active = 1, .note_wait = 1, .volume = 127, .expression = 127,
                            .pan = 64, .priority = 64, .bend_range = 2, .porta_key = 60, .attack = 0xff,
                            .decay = 0xff, .sustain = 0xff, .release = 0xff, .mod_speed = 16, .mod_range = 1,
                            .tie_channel = 0xff};
}

static void release_channel(PortableNdsChannel *c)
{
    if (c->active && c->state != RELEASE)
        c->state = RELEASE;
}

static void release_track(PortableNdsPlayer *p, uint8_t track)
{
    for (unsigned i = 0; i < PORTABLE_NDS_CHANNELS; ++i)
        if (p->channels[i].active && p->channels[i].track == track)
            release_channel(&p->channels[i]);
    p->tracks[track].tie_channel = 0xff;
}

/* Region for a key of a program, or NULL. */
static const uint8_t *find_region(const PortableNdsPlayer *p, uint16_t program, uint8_t key)
{
    if (program >= p->song.program_count)
        return NULL;
    const uint8_t *entry = p->song.bytes + p->song.program_offset + program * 4u;
    const uint16_t first = u16le(entry), count = u16le(entry + 2);
    for (uint16_t i = 0; i < count; ++i)
    {
        const uint8_t *r = p->song.bytes + p->song.region_offset + (uint32_t)(first + i) * 12u;
        if (key >= r[0] && key <= r[1])
            return r;
    }
    return NULL;
}

/* A free voice, else the quietest releasing one, else the lowest priority below ours. */
static PortableNdsChannel *allocate_channel(PortableNdsPlayer *p, uint8_t priority)
{
    PortableNdsChannel *best = NULL;
    for (unsigned i = 0; i < PORTABLE_NDS_CHANNELS; ++i)
    {
        PortableNdsChannel *c = &p->channels[i];
        if (!c->active)
            return c;
        if (c->state == RELEASE && (!best || best->state != RELEASE || c->amplitude < best->amplitude))
            best = c;
    }
    if (best)
        return best;
    for (unsigned i = 0; i < PORTABLE_NDS_CHANNELS; ++i)
        if (p->channels[i].priority < priority && (!best || p->channels[i].priority < best->priority))
            best = &p->channels[i];
    return best;
}

static void decode_sample(PortableNdsChannel *c)
{
    if (c->encoding == 0u)
        c->current = (int16_t)((int8_t)c->data[c->index] * 256);
    else if (c->encoding == 1u)
        c->current = (int16_t)u16le(c->data + c->index * 2u);
    else
    {
        if (c->loop && c->index == c->loop_start && !c->loop_saved)
        {
            c->loop_predictor = c->predictor;
            c->loop_step_index = c->step_index;
            c->loop_saved = 1;
        }
        const uint8_t byte = c->data[4u + c->index / 2u];
        const uint8_t nibble = (uint8_t)(c->index & 1u ? byte >> 4 : byte & 15u);
        const int32_t step = adpcm_step[c->step_index];
        int32_t diff = step >> 3;
        if (nibble & 1u)
            diff += step >> 2;
        if (nibble & 2u)
            diff += step >> 1;
        if (nibble & 4u)
            diff += step;
        c->predictor = nibble & 8u ? clamp(c->predictor - diff, -0x7fff, 0x7fff) : clamp(c->predictor + diff, -0x7fff, 0x7fff);
        c->step_index = clamp(c->step_index + adpcm_index[nibble & 7u], 0, 88);
        c->current = (int16_t)c->predictor;
    }
}

static void advance_sample(PortableNdsChannel *c)
{
    c->previous = c->current;
    if (c->kind == REGION_PSG)
    {
        c->phase = (uint8_t)((c->phase + 1u) & 7u);
        c->current = c->phase <= c->duty ? 0x7fff : -0x7fff;
        return;
    }
    if (c->kind == REGION_NOISE)
    {
        const unsigned carry = c->noise & 1u;
        c->noise = (uint16_t)(c->noise >> 1);
        if (carry)
            c->noise ^= 0x6000u;
        c->current = carry ? -0x7fff : 0x7fff;
        return;
    }
    if (c->ended)
    {
        c->active = 0;
        return;
    }
    if (++c->index >= c->sample_count)
    {
        if (!c->loop)
        {
            c->ended = 1;
            c->current = 0;
            return;
        }
        c->index = c->loop_start;
        if (c->encoding == 2u)
        {
            c->predictor = c->loop_predictor;
            c->step_index = c->loop_step_index;
            /* The saved state precedes the loop-start sample. */
            c->loop_saved = 0;
        }
    }
    decode_sample(c);
}

static void note_on(PortableNdsPlayer *p, uint8_t track, int32_t key, uint8_t velocity, int32_t length)
{
    PortableNdsTrack *t = &p->tracks[track];
    key = clamp(key + t->transpose, 0, 127);
    PortableNdsChannel *c = NULL;
    if (t->tie && t->tie_channel < PORTABLE_NDS_CHANNELS && p->channels[t->tie_channel].active &&
        p->channels[t->tie_channel].track == track)
        c = &p->channels[t->tie_channel];
    else
    {
        const uint8_t *r = find_region(p, t->program, (uint8_t)key);
        if (r && (c = allocate_channel(p, t->priority)) != NULL)
        {
            memset(c, 0, sizeof *c);
            c->kind = r[2];
            c->base_key = r[3];
            c->attack_rate = attack_rate(t->attack != 0xffu ? t->attack : r[4]);
            c->decay_rate = fall_rate(t->decay != 0xffu ? t->decay : r[5]);
            c->sustain_level = sustain_decibels(t->sustain != 0xffu ? t->sustain : r[6]) * 128;
            c->release_rate = fall_rate(t->release != 0xffu ? t->release : r[7]);
            c->pan = r[8];
            c->amplitude = AMPLITUDE_FLOOR;
            c->priority = t->priority;
            if (c->kind == REGION_PCM)
            {
                const uint8_t *w = p->song.bytes + p->song.wave_offset + u16le(r + 10) * 24u;
                c->encoding = w[0];
                c->loop = w[1];
                c->timer = u16le(w + 4);
                c->loop_start = u32le(w + 8);
                c->sample_count = u32le(w + 12);
                c->data = p->song.bytes + u32le(w + 16);
                if (c->encoding == 2u)
                {
                    c->predictor = (int16_t)u16le(c->data);
                    c->step_index = clamp(c->data[2], 0, 88);
                }
                decode_sample(c);
            }
            else if (c->kind == REGION_PSG)
                c->duty = (uint8_t)u16le(r + 10);
            else
                c->noise = 0x7fffu;
            c->previous = c->current;
            c->active = 1;
            c->track = track;
            c->mod_depth = t->mod_depth;
            c->mod_speed = t->mod_speed;
            c->mod_type = t->mod_type;
            c->mod_range = t->mod_range;
            c->mod_delay = t->mod_delay;
            if (t->tie)
                t->tie_channel = (uint8_t)(c - p->channels);
        }
    }
    if (c)
    {
        c->key = (uint8_t)key;
        c->velocity = velocity;
        c->length = t->tie || length <= 0 ? -1 : length;
        int32_t sweep = t->sweep_pitch;
        if (t->porta)
            sweep += (t->porta_key - key) * 64;
        c->sweep_pitch = (int16_t)clamp(sweep, -32768, 32767);
        c->sweep_counter = 0;
        if (!c->sweep_pitch)
            c->sweep_length = 0;
        else if (t->porta_time)
            c->sweep_length = (uint32_t)((t->porta_time * t->porta_time * (sweep < 0 ? -sweep : sweep)) >> 11);
        else
            /* Over the note: ticks to sound frames at the current tempo. */
            c->sweep_length = p->tempo ? (uint32_t)length * 240u / p->tempo : 0u;
    }
    t->porta_key = (uint8_t)key;
    if (t->note_wait)
    {
        t->wait = length;
        /* As NNS: a held note (length 0) waits for its sound to end. */
        t->note_finish_wait = length == 0;
    }
}

static int variable_command(PortableNdsPlayer *p, PortableNdsTrack *t, uint8_t command, uint8_t index, int32_t value)
{
    if (index >= 32u)
        return 0;
    int16_t *v = &p->variables[index];
    switch (command)
    {
    case 0xb0: *v = (int16_t)value; break;
    case 0xb1: *v = (int16_t)(*v + value); break;
    case 0xb2: *v = (int16_t)(*v - value); break;
    case 0xb3: *v = (int16_t)(*v * value); break;
    case 0xb4: if (value) *v = (int16_t)(*v / value); break;
    case 0xb5: *v = (int16_t)(value >= 0 ? *v << (value & 15) : *v >> (-value & 15)); break;
    case 0xb6:
        *v = (int16_t)(value >= 0 ? (int32_t)(next_random(p) % (uint32_t)(value + 1))
                                  : -(int32_t)(next_random(p) % (uint32_t)(-value + 1)));
        break;
    case 0xb8: t->compare = *v == value; break;
    case 0xb9: t->compare = *v >= value; break;
    case 0xba: t->compare = *v > value; break;
    case 0xbb: t->compare = *v <= value; break;
    case 0xbc: t->compare = *v < value; break;
    case 0xbd: t->compare = *v != value; break;
    default: return 0;
    }
    return 1;
}

static PortableNdsStatus run_command(PortableNdsPlayer *p, uint8_t track)
{
    PortableNdsTrack *t = &p->tracks[track];
    const uint32_t at = t->position;
    uint8_t command;
    unsigned prefix = 0, condition = 1;
    if (!read_byte(p, t, &command))
        return fail(p, PORTABLE_NDS_BAD_JUMP, at);
    while (command >= 0xa0u && command <= 0xa2u)
    {
        if (command == 0xa2u)
            condition = t->compare;
        else
            prefix = command;
        if (!read_byte(p, t, &command))
            return fail(p, PORTABLE_NDS_BAD_JUMP, at);
    }
    int32_t value = 0;
    uint32_t raw = 0;
    if (command < 0x80u)
    {
        uint8_t velocity;
        if (!read_byte(p, t, &velocity) || !read_argument(p, t, ARG_VARIABLE_LENGTH, prefix, &value))
            return fail(p, PORTABLE_NDS_BAD_JUMP, at);
        if (condition)
            note_on(p, track, command, velocity & 0x7fu, value);
        return PORTABLE_NDS_OK;
    }
    switch (command)
    {
    case 0x80:
    case 0x81:
        if (!read_argument(p, t, ARG_VARIABLE_LENGTH, prefix, &value))
            return fail(p, PORTABLE_NDS_BAD_JUMP, at);
        if (condition && command == 0x80u)
            t->wait = value;
        else if (condition)
            t->program = (uint16_t)value;
        return PORTABLE_NDS_OK;
    case 0x93:
    {
        uint32_t index;
        if (!read_fixed(p, t, 1, &index) || !read_fixed(p, t, 3, &raw))
            return fail(p, PORTABLE_NDS_BAD_JUMP, at);
        if (index >= PORTABLE_NDS_TRACKS || raw >= p->song.sequence_size)
            return fail(p, PORTABLE_NDS_BAD_JUMP, at);
        if (condition && index != track)
            reset_track(&p->tracks[index], raw);
        return PORTABLE_NDS_OK;
    }
    case 0x94:
    case 0x95:
        if (!read_fixed(p, t, 3, &raw) || raw >= p->song.sequence_size)
            return fail(p, PORTABLE_NDS_BAD_JUMP, at);
        if (!condition)
            return PORTABLE_NDS_OK;
        if (command == 0x95u)
        {
            if (t->depth >= PORTABLE_NDS_CALL_DEPTH)
                return fail(p, PORTABLE_NDS_BAD_JUMP, at);
            t->returns[t->depth] = t->position;
            t->loop_count[t->depth] = 0;
            t->depth++;
        }
        t->position = raw;
        return PORTABLE_NDS_OK;
    case 0xe0:
    case 0xe1:
    case 0xe3:
    case 0xfe:
        if (!read_argument(p, t, command == 0xe3u ? ARG_S16 : ARG_U16, prefix, &value))
            return fail(p, PORTABLE_NDS_BAD_JUMP, at);
        if (!condition)
            return PORTABLE_NDS_OK;
        if (command == 0xe0u)
            t->mod_delay = (uint16_t)value;
        else if (command == 0xe1u)
            p->tempo = (uint16_t)value;
        else if (command == 0xe3u)
            t->sweep_pitch = (int16_t)value;
        return PORTABLE_NDS_OK;
    case 0xfc:
        if (condition && t->depth)
        {
            const unsigned d = t->depth - 1u;
            /* Count 0 loops forever; otherwise the body runs `count` times. */
            if (!t->loop_count[d] || --t->loop_count[d])
                t->position = t->loop_start[d];
            else
                t->depth--;
        }
        return PORTABLE_NDS_OK;
    case 0xfd:
        if (condition && t->depth)
            t->position = t->returns[--t->depth];
        return PORTABLE_NDS_OK;
    case 0xff:
        if (condition)
        {
            t->active = 0;
            release_track(p, track);
        }
        return PORTABLE_NDS_OK;
    default:
        break;
    }
    if (command >= 0xb0u && command <= 0xbdu && command != 0xb7u)
    {
        uint32_t index;
        if (!read_fixed(p, t, 1, &index) || !read_argument(p, t, ARG_S16, prefix, &value))
            return fail(p, PORTABLE_NDS_BAD_JUMP, at);
        if (condition && !variable_command(p, t, command, (uint8_t)index, value))
            return fail(p, PORTABLE_NDS_BAD_COMMAND, at);
        return PORTABLE_NDS_OK;
    }
    if (command < 0xc0u || command > 0xd6u)
        return fail(p, PORTABLE_NDS_BAD_COMMAND, at);
    if (!read_argument(p, t, command == 0xc3u || command == 0xc4u ? ARG_S8 : ARG_U8, prefix, &value))
        return fail(p, PORTABLE_NDS_BAD_JUMP, at);
    if (!condition)
        return PORTABLE_NDS_OK;
    const uint8_t u = (uint8_t)value;
    switch (command)
    {
    case 0xc0: t->pan = (uint8_t)clamp(value, 0, 127); break;
    case 0xc1: t->volume = u; break;
    case 0xc2: p->master_volume = u; break;
    case 0xc3: t->transpose = (int8_t)value; break;
    case 0xc4: t->bend = (int8_t)value; break;
    case 0xc5: t->bend_range = u; break;
    case 0xc6: t->priority = u; break;
    case 0xc7: t->note_wait = u ? 1u : 0u; break;
    case 0xc8:
        release_track(p, track);
        t->tie = u ? 1u : 0u;
        break;
    case 0xc9:
        t->porta_key = (uint8_t)clamp(value + t->transpose, 0, 127);
        t->porta = 1;
        break;
    case 0xca: t->mod_depth = u; break;
    case 0xcb: t->mod_speed = u; break;
    case 0xcc: t->mod_type = u; break;
    case 0xcd: t->mod_range = u; break;
    case 0xce: t->porta = u ? 1u : 0u; break;
    case 0xcf: t->porta_time = u; break;
    case 0xd0: t->attack = u; break;
    case 0xd1: t->decay = u; break;
    case 0xd2: t->sustain = u; break;
    case 0xd3: t->release = u; break;
    case 0xd4:
        if (t->depth >= PORTABLE_NDS_CALL_DEPTH)
            return fail(p, PORTABLE_NDS_BAD_JUMP, at);
        t->loop_start[t->depth] = t->position;
        t->loop_count[t->depth] = u;
        t->returns[t->depth] = 0;
        t->depth++;
        break;
    case 0xd5: t->expression = u; break;
    default: break; /* 0xd6 prints a variable */
    }
    return PORTABLE_NDS_OK;
}

static PortableNdsStatus sequence_tick(PortableNdsPlayer *p)
{
    for (unsigned i = 0; i < PORTABLE_NDS_CHANNELS; ++i)
    {
        PortableNdsChannel *c = &p->channels[i];
        if (c->active && c->length > 0 && --c->length == 0)
            release_channel(c);
    }
    for (uint8_t i = 0; i < PORTABLE_NDS_TRACKS; ++i)
    {
        PortableNdsTrack *t = &p->tracks[i];
        if (!t->active)
            continue;
        if (t->note_finish_wait)
        {
            unsigned sounding = 0;
            for (unsigned c = 0; c < PORTABLE_NDS_CHANNELS && !sounding; ++c)
                sounding = p->channels[c].active && p->channels[c].track == i;
            if (sounding)
                continue;
            t->note_finish_wait = 0;
        }
        if (t->wait > 0 && --t->wait > 0)
            continue;
        for (unsigned n = 0; t->active && t->wait <= 0 && !t->note_finish_wait; ++n)
        {
            if (n == COMMAND_LIMIT)
                return fail(p, PORTABLE_NDS_RUNAWAY_TRACK, t->position);
            if (run_command(p, i) != PORTABLE_NDS_OK)
                return p->status;
        }
    }
    return PORTABLE_NDS_OK;
}

static int32_t lfo_value(PortableNdsChannel *c)
{
    int32_t value = 0;
    if (c->mod_depth && c->lfo_delay_counter >= c->mod_delay)
    {
        const unsigned index = (unsigned)(c->lfo_counter >> 8) & 127u;
        const int32_t sine = index < 32u ? quarter_sine[index] : index < 64u ? quarter_sine[64u - index]
                           : index < 96u ? -quarter_sine[index - 64u] : -quarter_sine[128u - index];
        value = sine * c->mod_depth * c->mod_range;
    }
    if (c->lfo_delay_counter < c->mod_delay)
        c->lfo_delay_counter++;
    else
        c->lfo_counter = (uint16_t)((c->lfo_counter + (c->mod_speed << 6)) & 0x7fffu);
    return value;
}

static void channel_frame(PortableNdsPlayer *p, PortableNdsChannel *c)
{
    switch (c->state)
    {
    case ATTACK:
        c->amplitude = c->amplitude * c->attack_rate / 256;
        if (c->amplitude == 0)
            c->state = DECAY;
        break;
    case DECAY:
        c->amplitude -= c->decay_rate;
        if (c->amplitude <= c->sustain_level)
        {
            c->amplitude = c->sustain_level;
            c->state = SUSTAIN;
        }
        break;
    case RELEASE:
        c->amplitude -= c->release_rate;
        if (c->amplitude <= AMPLITUDE_FLOOR)
        {
            c->active = 0;
            return;
        }
        break;
    default:
        break;
    }
    const PortableNdsTrack *t = &p->tracks[c->track];
    /* Player pitch is 1/256 semitone (as the GBA mixer's); channels use 1/64. */
    int32_t pitch = (c->key - c->base_key) * 64 + t->bend * t->bend_range / 2 + p->pitch / 4;
    if (c->sweep_length && c->sweep_counter < c->sweep_length)
    {
        pitch += (int32_t)((int64_t)c->sweep_pitch * (c->sweep_length - c->sweep_counter) / c->sweep_length);
        c->sweep_counter++;
    }
    int32_t level = (c->amplitude >> 7) + sustain_decibels(c->velocity) + sustain_decibels(t->volume) +
                    sustain_decibels(t->expression) + sustain_decibels(p->master_volume) +
                    sustain_decibels(p->song.volume);
    int32_t pan = t->pan + c->pan - 64;
    const int32_t lfo = lfo_value(c);
    if (c->mod_type == 1u)
        level += (int32_t)(((int64_t)lfo * 60) >> 14);
    else if (c->mod_type == 2u)
        pan += (int32_t)(((int64_t)lfo * 64) >> 14);
    else
        pitch += (int32_t)(((int64_t)lfo * 64) >> 14);
    const int32_t gain = (int32_t)((int64_t)decibel_gain(clamp(level, -723, 0)) * p->volume >> 8);
    pan = clamp(pan, 0, 127);
    c->gain_left = gain * (127 - pan) / 127;
    c->gain_right = gain * pan / 127;
    const uint64_t factor = exp2_q16(clamp(pitch, -768 * 10, 768 * 10) * 256 / 3);
    /* PCM plays at 16756991 / timer Hz at its base key; PSG takes eight steps per
     * period with A4 (key 69) at 440 Hz. */
    const uint64_t base = c->kind == REGION_PCM
                              ? (CHANNEL_CLOCK << 32) / ((uint64_t)c->timer * PORTABLE_AUDIO_PCM_RATE)
                              : (UINT64_C(3520) << 32) / PORTABLE_AUDIO_PCM_RATE;
    c->step = ((base >> 8) * factor) >> 8;
}

static PortableNdsStatus sound_frame(PortableNdsPlayer *p)
{
    p->tempo_counter += (uint32_t)p->tempo * p->tempo_scale >> 8;
    while (p->tempo_counter >= 240u)
    {
        p->tempo_counter -= 240u;
        if (sequence_tick(p) != PORTABLE_NDS_OK)
            return p->status;
    }
    unsigned voices = 0, tracks = 0;
    for (unsigned i = 0; i < PORTABLE_NDS_CHANNELS; ++i)
        if (p->channels[i].active)
        {
            channel_frame(p, &p->channels[i]);
            voices += p->channels[i].active;
        }
    for (unsigned i = 0; i < PORTABLE_NDS_TRACKS; ++i)
        tracks += p->tracks[i].active;
    if (!voices && !tracks)
        p->active = 0;
    return PORTABLE_NDS_OK;
}

void portable_nds_player_start(PortableNdsPlayer *player, const PortableNdsSequenceView *song)
{
    if (!player || !song)
        return;
    memset(player, 0, sizeof *player);
    player->song = *song;
    reset_track(&player->tracks[0], song->start);
    player->tempo = 120;
    player->tempo_scale = 256;
    player->volume = 256;
    player->master_volume = 127;
    player->random = 0x12345678u;
    player->clock = FRAME_CYCLES * PORTABLE_AUDIO_PCM_RATE;
    player->active = 1;
}

void portable_nds_player_stop(PortableNdsPlayer *player)
{
    if (player)
        memset(player, 0, sizeof *player);
}

int portable_nds_player_playing(const PortableNdsPlayer *player)
{
    return player && player->active && !player->paused;
}

PortableNdsStatus portable_nds_player_mix(PortableNdsPlayer *p, int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 2u])
{
    if (!p || !output)
        return PORTABLE_NDS_INVALID_ARGUMENT;
    if (p->status != PORTABLE_NDS_OK || !p->active || p->paused)
        return p->status;
    for (unsigned s = 0; s < PORTABLE_AUDIO_PCM_FRAME_SAMPLES && p->active; ++s)
    {
        p->clock += ARM7_CLOCK;
        while (p->clock >= FRAME_CYCLES * PORTABLE_AUDIO_PCM_RATE)
        {
            p->clock -= FRAME_CYCLES * PORTABLE_AUDIO_PCM_RATE;
            if (sound_frame(p) != PORTABLE_NDS_OK)
                return p->status;
        }
        int32_t left = 0, right = 0;
        for (unsigned i = 0; i < PORTABLE_NDS_CHANNELS; ++i)
        {
            PortableNdsChannel *c = &p->channels[i];
            if (!c->active)
                continue;
            const int32_t sample = c->previous +
                                   (int32_t)(((int64_t)(c->current - c->previous) * (int64_t)(c->fraction >> 16)) >> 16);
            left += (int32_t)(((int64_t)sample * c->gain_left) >> 16);
            right += (int32_t)(((int64_t)sample * c->gain_right) >> 16);
            c->fraction += c->step;
            while (c->active && c->fraction >= (UINT64_C(1) << 32))
            {
                c->fraction -= UINT64_C(1) << 32;
                advance_sample(c);
            }
        }
        output[s * 2u] = (int16_t)clamp(output[s * 2u] + left / 2, -32768, 32767);
        output[s * 2u + 1u] = (int16_t)clamp(output[s * 2u + 1u] + right / 2, -32768, 32767);
    }
    return p->status;
}

const char *portable_nds_status_name(PortableNdsStatus status)
{
    static const char *const names[] = {"ok", "invalid argument", "bad NDS sequence section",
                                        "unsupported SSEQ command", "SSEQ read or jump out of range",
                                        "SSEQ track runs without waiting"};
    return (unsigned)status < sizeof names / sizeof names[0] ? names[status] : "unknown status";
}
