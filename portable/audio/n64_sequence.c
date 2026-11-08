#include "n64_sequence.h"

#include <math.h>
#include <string.h>

/* NTSC Mario Kart 64: 26800 Hz output, three audio updates per frame, one
 * tatum whenever the tempo accumulator passes 3 * 2880000 / 48 / 16.713. */
#define UPDATES 3u
#define TEMPO_LIMIT 10770u
#define TEMPO_SCALE 48u
#define SOURCE_RATE 26800.0f
#define SCRIPT_LIMIT 4096u
#define RELEASE_SCALE (0.001171875f / UPDATES)
#define UPDATE_INVERSE (1.0f / UPDATES)
#define REVERB_GAIN 0.625f /* aMix 0x8000 + 0x4fff on the ring buffer */

enum { PRIORITY_DISABLED = 0, PRIORITY_STOPPING = 1, PRIORITY_MIN = 2, PRIORITY_DEFAULT = 3 };
enum { LIST_DISABLED = 0, LIST_ACTIVE, LIST_DECAYING, LIST_RELEASING };
enum { ADSR_DISABLED = 0, ADSR_INITIAL, ADSR_START_LOOP, ADSR_LOOP, ADSR_FADE, ADSR_HANG, ADSR_DECAY, ADSR_RELEASE,
       ADSR_SUSTAIN };
enum { ACTION_RELEASE = 0x10, ACTION_DECAY = 0x20, ACTION_HANG = 0x40 };
enum { MUTE_STOP_SCRIPT = 0x80, MUTE_STOP_NOTES = 0x40, MUTE_SOFTEN = 0x20 };
enum { ALLOC_LAYER = 1 };
enum { CHANGE_VOLUME = 1, CHANGE_PAN = 2, CHANGE_FREQ = 4 };
enum { STATE_0 = 0, STATE_FADE_OUT = 1, STATE_2 = 2 };
#define NO_SOUND 0xffffffffu

static const uint8_t default_velocity[16] = {12, 25, 38, 51, 57, 64, 71, 76, 83, 89, 96, 102, 109, 115, 121, 127};
static const uint8_t default_duration[16] = {229, 203, 177, 151, 139, 126, 113, 100, 87, 74, 61, 48, 36, 23, 10, 0};
/* gDefaultEnvelope: rise over 4 updates, hold, hang. */
static const int16_t default_envelope[6] = {4, 32000, 1000, 32000, -1, 0};
static const uint8_t wave_block_size[4] = {0x40, 0x20, 0x10, 0x08};

static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static int16_t be16s(const uint8_t *p) { return (int16_t)(uint16_t)(p[0] << 8 | p[1]); }
static float lef32(const uint8_t *p)
{
    const uint32_t bits = le32(p);
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}
static int span(uint32_t size, uint32_t offset, uint64_t length) { return offset <= size && length <= size - offset; }
static int16_t clamp16(int32_t v) { return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v); }

/* gNoteFrequencies: semitone 39 plays at the sample's own rate; the top
 * octave wraps down one (the table's notes 117..127). */
static float note_frequency(uint32_t note)
{
    const float f = powf(2.0f, ((float)note - 39.0f) / 12.0f);
    return note >= 117u ? f * 0.5f : f;
}
/* gPitchBendFrequencyScale: index 128 is unity, one octave either side. */
static float pitch_bend(uint32_t index)
{
    return index == 0u ? 0.5f : 0.5f * powf(2.0f, ((float)index - 1.0f) / 127.0f);
}
/* gDefaultPanVolume. */
static float pan_volume(uint32_t pan) { return cosf((float)pan * 1.5707963f / 127.0f); }
/* sSineWaves' first cycle, the vibrato curve. */
static int32_t sine64(uint32_t index) { return (int32_t)(32767.0f * sinf((float)(index & 63u) * 6.2831853f / 64.0f)); }

/* Synthetic waves 0x80.. (saw, triangle, sine, square): block k holds 2^k
 * cycles in 64 samples. */
static int16_t synthetic_sample(uint32_t wave, uint32_t block, uint32_t position)
{
    const uint32_t phase = (position * (1u << block)) & 63u;
    switch (wave)
    {
    case 0: return (int16_t)(phase < 32u ? (int32_t)phase * 1024 : ((int32_t)phase - 64) * 1024);
    case 1: return (int16_t)(phase < 16u ? (int32_t)phase * 2048 : phase < 48u ? 32767 - ((int32_t)phase - 16) * 2048
                                                                             : ((int32_t)phase - 64) * 2048);
    case 2: return (int16_t)sine64(phase);
    default: return (int16_t)(phase < 32u ? 32767 : -32767);
    }
}

/* ---- Section ---------------------------------------------------------------- */

typedef struct
{
    uint8_t present, low, high, release;
    uint32_t envelope; /* section offset */
    uint16_t sample[3];
    float tuning[3];
} Instrument;

typedef struct
{
    uint8_t present, release, pan;
    uint32_t envelope;
    uint16_t sample;
    float tuning;
} Drum;

typedef struct
{
    uint32_t data, size, book, loop_start, loop_end, loop_count, state;
} Sample;

static void read_instrument(const PortableN64SequenceView *s, uint32_t index, Instrument *out)
{
    const uint8_t *r = s->bytes + s->instrument_offset + index * 32u;
    *out = (Instrument){r[0], r[1], r[2], r[3], s->data_offset + le32(r + 4), {0}, {0}};
    for (unsigned i = 0; i < 3u; ++i)
    {
        out->sample[i] = (uint16_t)le16(r + 8 + i * 8u);
        out->tuning[i] = lef32(r + 12 + i * 8u);
    }
}

static void read_drum(const PortableN64SequenceView *s, uint32_t index, Drum *out)
{
    const uint8_t *r = s->bytes + s->drum_offset + index * 16u;
    *out = (Drum){r[0], r[1], r[2], s->data_offset + le32(r + 4), (uint16_t)le16(r + 8), lef32(r + 12)};
}

static void read_sample(const PortableN64SequenceView *s, uint32_t index, Sample *out)
{
    const uint8_t *r = s->bytes + s->sample_offset + index * 28u;
    *out = (Sample){s->data_offset + le32(r), le32(r + 4), s->data_offset + le32(r + 8), le32(r + 12), le32(r + 16),
                    le32(r + 20), s->data_offset + le32(r + 24)};
}

static int envelope_valid(const PortableN64SequenceView *s, uint32_t data_offset)
{
    /* At most 32 points, ending in disable, hang, restart or a goto back. */
    for (uint32_t i = 0; i < 32u; ++i)
    {
        if (!span(s->data_size, data_offset, (uint64_t)i * 4u + 4u))
            return 0;
        const int16_t delay = be16s(s->bytes + s->data_offset + data_offset + i * 4u);
        if (delay == 0 || delay == -1 || delay == -3)
            return 1;
        if (delay == -2)
            return (uint16_t)be16s(s->bytes + s->data_offset + data_offset + i * 4u + 2u) <= i;
        if (delay < 0)
            return 0;
    }
    return 0;
}

PortableN64Status portable_n64_sequence_open(PortableN64SequenceView *view, const uint8_t *bytes, size_t size)
{
    if (!view || !bytes)
        return PORTABLE_N64_INVALID_ARGUMENT;
    memset(view, 0, sizeof *view);
    if (size < PORTABLE_N64_SEQUENCE_HEADER_SIZE || size > UINT32_MAX || memcmp(bytes, "MKSCN64S", 8) ||
        le16(bytes + 8) != PORTABLE_N64_SEQUENCE_HEADER_SIZE || le16(bytes + 10) != 1u)
        return PORTABLE_N64_BAD_SECTION;
    PortableN64SequenceView v = {.bytes = bytes, .size = (uint32_t)size, .player = bytes[12],
                                 .variation = (int8_t)bytes[13], .follow = (uint16_t)le16(bytes + 14),
                                 .sequence_offset = le32(bytes + 24), .sequence_size = le32(bytes + 28),
                                 .instrument_offset = le32(bytes + 32), .instrument_count = le32(bytes + 36),
                                 .drum_offset = le32(bytes + 40), .drum_count = le32(bytes + 44),
                                 .sample_offset = le32(bytes + 48), .sample_count = le32(bytes + 52),
                                 .data_offset = le32(bytes + 56), .data_size = le32(bytes + 60)};
    memcpy(v.effect_channels, bytes + 16, sizeof v.effect_channels);
    memcpy(v.bank_players, bytes + 64, sizeof v.bank_players);
    if (!v.sequence_size || v.sequence_size > 0x10000u || !span(v.size, v.sequence_offset, v.sequence_size) ||
        v.instrument_count > 0x7fu || !span(v.size, v.instrument_offset, (uint64_t)v.instrument_count * 32u) ||
        v.drum_count > 0x100u || !span(v.size, v.drum_offset, (uint64_t)v.drum_count * 16u) ||
        v.sample_count > 0xffffu || !span(v.size, v.sample_offset, (uint64_t)v.sample_count * 28u) ||
        !span(v.size, v.data_offset, v.data_size))
        return PORTABLE_N64_BAD_SECTION;
    for (unsigned i = 0; i < PORTABLE_N64_SOUND_BANKS; ++i)
        if (v.effect_channels[i] && (v.effect_channels[i] & 15u) + (v.effect_channels[i] >> 4) > PORTABLE_N64_CHANNELS)
            return PORTABLE_N64_BAD_SECTION;
    for (uint32_t i = 0; i < v.sample_count; ++i)
    {
        const uint8_t *r = bytes + v.sample_offset + i * 28u;
        const uint32_t data = le32(r), length = le32(r + 4), book = le32(r + 8), start = le32(r + 12),
                       end = le32(r + 16), count = le32(r + 20), state = le32(r + 24);
        if (!span(v.data_size, data, length) || !span(v.data_size, book, 8u))
            return PORTABLE_N64_BAD_SECTION;
        const uint32_t order = be32(bytes + v.data_offset + book), predictors = be32(bytes + v.data_offset + book + 4);
        if (order != 2u || !predictors || predictors > 16u ||
            !span(v.data_size, book + 8u, (uint64_t)predictors * 32u) || end > length / 9u * 16u ||
            (count && (start >= end || !span(v.data_size, state, 32u))))
            return PORTABLE_N64_BAD_SECTION;
    }
    for (uint32_t i = 0; i < v.instrument_count; ++i)
    {
        const uint8_t *r = bytes + v.instrument_offset + i * 32u;
        if (r[0] > 1u || (r[0] && !envelope_valid(&v, le32(r + 4))))
            return PORTABLE_N64_BAD_SECTION;
        for (unsigned s = 0; s < 3u; ++s)
            if (le16(r + 8 + s * 8u) != 0xffffu && le16(r + 8 + s * 8u) >= v.sample_count)
                return PORTABLE_N64_BAD_SECTION;
    }
    for (uint32_t i = 0; i < v.drum_count; ++i)
    {
        const uint8_t *r = bytes + v.drum_offset + i * 16u;
        if (r[0] > 1u || (r[0] && (!envelope_valid(&v, le32(r + 4)) ||
                                   (le16(r + 8) != 0xffffu && le16(r + 8) >= v.sample_count))))
            return PORTABLE_N64_BAD_SECTION;
    }
    *view = v;
    return PORTABLE_N64_OK;
}

/* ---- VADPCM ----------------------------------------------------------------- */

void portable_n64_vadpcm_frame(const uint8_t frame[9], const uint8_t *book, uint32_t predictors,
                               int16_t history[2], int16_t out[16])
{
    const uint32_t scale = frame[0] >> 4, predictor = frame[0] & 15u;
    const uint8_t *coefficients = book + (predictor < predictors ? predictor : 0u) * 32u;
    int32_t c[2][8];
    for (unsigned o = 0; o < 2u; ++o)
        for (unsigned i = 0; i < 8u; ++i)
            c[o][i] = be16s(coefficients + (o * 8u + i) * 2u);
    int32_t previous2 = history[0], previous1 = history[1];
    for (unsigned half = 0; half < 2u; ++half)
    {
        int32_t ix[8];
        for (unsigned i = 0; i < 8u; ++i)
        {
            const uint8_t byte = frame[1 + half * 4u + i / 2u];
            int32_t nibble = (i & 1u) ? byte & 15 : byte >> 4;
            if (nibble >= 8)
                nibble -= 16;
            ix[i] = nibble * (1 << scale);
        }
        for (unsigned i = 0; i < 8u; ++i)
        {
            int32_t total = c[0][i] * previous2 + c[1][i] * previous1;
            for (unsigned k = 0; k < i; ++k)
                total += c[1][i - k - 1u] * ix[k];
            /* The RSP's >> 11 floors. */
            out[half * 8u + i] = clamp16((total >> 11) + ix[i]);
        }
        previous2 = out[half * 8u + 6u];
        previous1 = out[half * 8u + 7u];
    }
    history[0] = (int16_t)previous2;
    history[1] = (int16_t)previous1;
}

/* ---- Script bytes ----------------------------------------------------------- */

static PortableN64Status fail(PortableN64Player *p, PortableN64Status status, uint32_t at)
{
    if (p->status == PORTABLE_N64_OK)
    {
        p->status = status;
        p->failed_at = at;
    }
    p->enabled = 0;
    p->active = 0;
    return status;
}

static uint8_t seq_byte(PortableN64Player *p, uint32_t at)
{
    if (at >= p->song.sequence_size)
    {
        fail(p, PORTABLE_N64_BAD_ADDRESS, at);
        return 0xff;
    }
    for (unsigned i = 0; i < p->patch_count; ++i)
        if (p->patch_at[i] == at)
            return p->patch_value[i];
    return p->song.bytes[p->song.sequence_offset + at];
}

static void seq_write(PortableN64Player *p, uint32_t at, uint8_t value)
{
    if (at >= p->song.sequence_size)
    {
        fail(p, PORTABLE_N64_BAD_ADDRESS, at);
        return;
    }
    for (unsigned i = 0; i < p->patch_count; ++i)
        if (p->patch_at[i] == at)
        {
            p->patch_value[i] = value;
            return;
        }
    if (p->patch_count >= PORTABLE_N64_PATCHES)
    {
        fail(p, PORTABLE_N64_RUNAWAY_SCRIPT, at);
        return;
    }
    p->patch_at[p->patch_count] = (uint16_t)at;
    p->patch_value[p->patch_count++] = value;
}

static uint8_t rd8(PortableN64Player *p, PortableN64Script *s) { return seq_byte(p, s->pc++); }
static uint16_t rd16(PortableN64Player *p, PortableN64Script *s)
{
    const uint32_t high = rd8(p, s);
    return (uint16_t)(high << 8 | rd8(p, s));
}
static uint16_t rdvar(PortableN64Player *p, PortableN64Script *s)
{
    uint32_t v = rd8(p, s);
    if (v & 0x80u)
        v = ((v << 8) & 0x7f00u) | rd8(p, s);
    return (uint16_t)v;
}
static void push(PortableN64Player *p, PortableN64Script *s, uint32_t pc)
{
    if (s->depth >= PORTABLE_N64_STACK)
    {
        fail(p, PORTABLE_N64_RUNAWAY_SCRIPT, s->pc);
        return;
    }
    s->stack[s->depth++] = pc;
}
static uint32_t dyn_entry(PortableN64Player *p, uint32_t table, int8_t value)
{
    const uint32_t at = table + (uint32_t)(int32_t)value * 2u;
    const uint32_t high = seq_byte(p, at);
    return (uint32_t)(uint16_t)(high << 8 | seq_byte(p, at + 1u));
}

/* Envelope point `index`: section offset, or 0 for the default envelope. */
static void envelope_point(PortableN64Player *p, uint32_t envelope, uint32_t index, int16_t *delay, int16_t *arg)
{
    if (envelope == 0u)
    {
        index = index < 3u ? index : 2u;
        *delay = default_envelope[index * 2u];
        *arg = default_envelope[index * 2u + 1u];
        return;
    }
    if (!span(p->song.size, envelope, (uint64_t)index * 4u + 4u))
    {
        fail(p, PORTABLE_N64_BAD_ADDRESS, envelope);
        *delay = 0;
        *arg = 0;
        return;
    }
    *delay = be16s(p->song.bytes + envelope + index * 4u);
    *arg = be16s(p->song.bytes + envelope + index * 4u + 2u);
}

/* ---- Notes ------------------------------------------------------------------ */

static PortableN64Channel *layer_channel(PortableN64Player *p, int16_t layer) { return &p->channels[layer / 4]; }
static PortableN64Layer *layer_at(PortableN64Player *p, int16_t layer) { return &p->channels[layer / 4].layers[layer % 4]; }

/* The driver's note lists: push_back appends, push_front prepends,
 * pop_back takes the last. */
static void list_back(PortableN64Player *p, PortableN64Note *n, uint8_t list)
{
    n->list = list;
    n->order = ++p->back_order;
}
static void list_front(PortableN64Player *p, PortableN64Note *n, uint8_t list)
{
    n->list = list;
    n->order = --p->front_order;
}
static int list_pop_back(PortableN64Player *p, uint8_t list)
{
    int best = -1;
    for (unsigned i = 0; i < PORTABLE_N64_NOTES; ++i)
        if (p->notes[i].list == list && (best < 0 || p->notes[i].order > p->notes[best].order))
            best = (int)i;
    return best;
}

static void adsr_init(PortableN64AdsrState *adsr, uint32_t envelope)
{
    *adsr = (PortableN64AdsrState){.envelope = envelope, .state = ADSR_DISABLED};
}

static float adsr_update(PortableN64Player *p, PortableN64AdsrState *adsr)
{
    const uint8_t action = adsr->action, state = adsr->state;
    int16_t delay, arg;
    switch (state)
    {
    case ADSR_DISABLED:
        return 0.0f;
    case ADSR_INITIAL:
        if (action & ACTION_HANG)
        {
            adsr->state = ADSR_HANG;
            break;
        }
        /* fallthrough */
    case ADSR_START_LOOP:
        adsr->index = 0;
        adsr->state = ADSR_LOOP;
        /* fallthrough */
    case ADSR_LOOP:
        for (unsigned guard = 0; guard < 32u; ++guard)
        {
            envelope_point(p, adsr->envelope, adsr->index, &delay, &arg);
            adsr->delay = delay;
            if (delay == -2)
            {
                adsr->index = (uint8_t)arg;
                continue;
            }
            if (delay == 0)
                adsr->state = ADSR_DISABLED;
            else if (delay == -1)
                adsr->state = ADSR_HANG;
            else if (delay == -3)
                adsr->state = ADSR_INITIAL;
            else
            {
                if (adsr->delay >= 4)
                    adsr->delay = (int16_t)(adsr->delay * (int32_t)UPDATES / 1 / 4);
                if (adsr->delay == 0)
                    adsr->delay = 1;
                adsr->target = (float)arg / 32767.0f;
                adsr->target *= adsr->target;
                adsr->velocity = (adsr->target - adsr->current) / adsr->delay;
                adsr->state = ADSR_FADE;
                adsr->index++;
            }
            break;
        }
        if (adsr->state != ADSR_FADE)
            break;
        /* fallthrough */
    case ADSR_FADE:
        adsr->current += adsr->velocity;
        if (--adsr->delay <= 0)
            adsr->state = ADSR_LOOP;
        break;
    case ADSR_HANG:
        break;
    case ADSR_DECAY:
    case ADSR_RELEASE:
        adsr->current -= adsr->fade_out;
        if (adsr->sustain != 0.0f && state == ADSR_DECAY)
        {
            if (adsr->current < adsr->sustain)
            {
                adsr->current = adsr->sustain;
                adsr->delay = 128;
                adsr->state = ADSR_SUSTAIN;
            }
            break;
        }
        if (adsr->current < 0.00001f)
        {
            adsr->current = 0.0f;
            adsr->state = ADSR_DISABLED;
        }
        break;
    case ADSR_SUSTAIN:
        if (--adsr->delay == 0)
            adsr->state = ADSR_RELEASE;
        break;
    }
    if (action & ACTION_DECAY)
    {
        adsr->state = ADSR_DECAY;
        adsr->action = (uint8_t)(action & ~ACTION_DECAY);
    }
    if (action & ACTION_RELEASE)
    {
        adsr->state = ADSR_RELEASE;
        adsr->action = (uint8_t)(action & ~ACTION_RELEASE);
    }
    return adsr->current < 0.0f ? 0.0f : adsr->current > 1.0f ? 1.0f : adsr->current;
}

static void set_vel_pan_reverb(PortableN64Note *n, float velocity, uint8_t pan, uint8_t reverb)
{
    pan &= 0x7fu;
    velocity = velocity < 0.0f ? 0.0f : velocity > 1.0f ? 1.0f : velocity;
    n->target_left = (int32_t)(velocity * pan_volume(pan) * 4095.999f) << 4;
    n->target_right = (int32_t)(velocity * pan_volume(127u - pan) * 4095.999f) << 4;
    n->reverb = reverb;
}

static void set_resampling(PortableN64Player *p, PortableN64Note *n, float frequency)
{
    if (p->pitch)
        frequency *= powf(2.0f, (float)p->pitch / 3072.0f);
    if (frequency < 0.0f)
        frequency = 0.0f;
    /* One ADPCM part below 2x, two above, each capped at 1.99996. */
    const float cap = frequency < 2.0f ? 1.9999599f : 2.0f * 1.9999599f;
    if (frequency > cap)
        frequency = cap;
    n->step = (uint32_t)(frequency * (SOURCE_RATE / (float)PORTABLE_AUDIO_PCM_RATE) * 65536.0f);
}

static void build_synthetic_wave(PortableN64Note *n, PortableN64Layer *layer, uint32_t wave)
{
    if (wave < 128u)
        wave = 128u;
    float freq = layer->freq_scale;
    if (layer->portamento.mode && layer->portamento.extent > 0.0f)
        freq *= layer->portamento.extent + 1.0f;
    uint8_t block;
    float ratio;
    if (freq < 1.0f) block = 0, ratio = 1.0465f;
    else if (freq < 2.0f) block = 1, ratio = 0.52325f;
    else if (freq < 4.0f) block = 2, ratio = 0.26263f;
    else block = 3, ratio = 0.13081f;
    layer->freq_scale *= ratio;
    n->wave = (uint8_t)(wave - 128u);
    n->wave_block = block;
}

static void note_disable(PortableN64Note *n)
{
    if (n->needs_init)
        n->needs_init = 0;
    else
        set_vel_pan_reverb(n, 0.0f, 0x40, 0);
    n->priority = PRIORITY_DISABLED;
    n->parent = -1;
    n->prev_parent = -1;
    n->enabled = 0;
    n->finished = 0;
}

static void note_init_for_layer(PortableN64Player *p, int note, int16_t layer_index)
{
    PortableN64Note *n = &p->notes[note];
    PortableN64Layer *layer = layer_at(p, layer_index);
    PortableN64Channel *channel = layer_channel(p, layer_index);
    n->prev_parent = -1;
    n->parent = layer_index;
    n->priority = channel->priority;
    layer->properties_init = 1;
    layer->status = 1;
    layer->note = (int8_t)note;
    layer->note_velocity = 0.0f;
    adsr_init(&n->adsr, layer->adsr.release == 0u ? channel->adsr.envelope : layer->adsr.envelope);
    n->adsr.state = ADSR_INITIAL;
    n->enabled = 1;
    n->needs_init = 1;
    n->finished = 0;
    n->reverb = 0;
    const uint32_t instrument = layer->inst_or_wave == 0xffu ? channel->inst_or_wave : layer->inst_or_wave;
    n->sample = layer->sample;
    n->synthetic = instrument >= 0x80u;
    if (n->synthetic)
        build_synthetic_wave(n, layer, instrument);
}

static void vibrato_init(PortableN64Player *p, int note)
{
    PortableN64Note *n = &p->notes[note];
    PortableN64Layer *layer = layer_at(p, n->parent);
    PortableN64Channel *c = layer_channel(p, n->parent);
    n->vibrato_scale = 1.0f;
    n->portamento_scale = 1.0f;
    PortableN64Vibrato *v = &n->vibrato;
    v->active = 1;
    v->time = 0;
    v->channel = (uint8_t)(n->parent / 4);
    if ((v->extent_timer = c->vibrato_extent_delay) == 0u)
        v->extent = c->vibrato_extent_target;
    else
        v->extent = c->vibrato_extent_start;
    if ((v->rate_timer = c->vibrato_rate_delay) == 0u)
        v->rate = c->vibrato_rate_target;
    else
        v->rate = c->vibrato_rate_start;
    v->delay = c->vibrato_delay;
    n->portamento = layer->portamento;
}

static float portamento_scale(PortableN64Portamento *m)
{
    m->cur += m->speed;
    uint32_t index = (uint32_t)m->cur;
    if (index > 127u)
        index = 127u;
    return 1.0f + m->extent * (pitch_bend(index + 128u) - 1.0f);
}

static float vibrato_scale(PortableN64Player *p, PortableN64Vibrato *v)
{
    const PortableN64Channel *c = &p->channels[v->channel];
    if (v->delay)
    {
        v->delay--;
        return 1.0f;
    }
    if (v->extent_timer)
    {
        if (v->extent_timer == 1u)
            v->extent = c->vibrato_extent_target;
        else
            v->extent += (float)(((int32_t)c->vibrato_extent_target - (int32_t)v->extent) / (int32_t)v->extent_timer);
        v->extent_timer--;
    }
    else if (c->vibrato_extent_target != (int32_t)v->extent)
    {
        if ((v->extent_timer = c->vibrato_extent_delay) == 0u)
            v->extent = c->vibrato_extent_target;
    }
    if (v->rate_timer)
    {
        if (v->rate_timer == 1u)
            v->rate = c->vibrato_rate_target;
        else
            v->rate += (float)(((int32_t)c->vibrato_rate_target - (int32_t)v->rate) / (int32_t)v->rate_timer);
        v->rate_timer--;
    }
    else if (c->vibrato_rate_target != (int32_t)v->rate)
    {
        if ((v->rate_timer = c->vibrato_rate_delay) == 0u)
            v->rate = c->vibrato_rate_target;
    }
    if (v->extent == 0.0f)
        return 1.0f;
    v->time += (uint32_t)(int32_t)v->rate;
    const int32_t change = sine64((v->time >> 10) & 63u) >> 8;
    return 1.0f + v->extent / 4096.0f * (pitch_bend((uint32_t)(change + 128)) - 1.0f);
}

/* seq_channel_layer_decay_release_internal. */
static void layer_decay_release(PortableN64Player *p, int16_t layer_index, uint8_t target)
{
    PortableN64Layer *layer = layer_at(p, layer_index);
    if (layer->note < 0)
        return;
    PortableN64Note *n = &p->notes[layer->note];
    if (n->wanted_parent == layer_index)
        n->wanted_parent = -1;
    if (n->parent != layer_index)
    {
        if (n->parent < 0 && n->wanted_parent < 0 && n->prev_parent == layer_index && target != ADSR_DECAY)
        {
            n->adsr.fade_out = UPDATE_INVERSE;
            n->adsr.action |= ACTION_RELEASE;
        }
        return;
    }
    layer->status = 0;
    if (n->adsr.state != ADSR_DECAY)
    {
        const PortableN64Channel *c = layer_channel(p, layer_index);
        n->attr_freq_scale = layer->note_freq_scale;
        n->attr_velocity = layer->note_velocity;
        n->attr_pan = layer->note_pan;
        n->attr_reverb = c->reverb;
        n->priority = PRIORITY_STOPPING;
        n->prev_parent = n->parent;
        n->parent = -1;
        if (target == ADSR_RELEASE)
        {
            n->adsr.fade_out = UPDATE_INVERSE;
            n->adsr.action |= ACTION_RELEASE;
        }
        else
        {
            n->adsr.action |= ACTION_DECAY;
            n->adsr.fade_out = (layer->adsr.release == 0u ? c->adsr.release : layer->adsr.release) * RELEASE_SCALE;
            n->adsr.sustain = (float)c->adsr.sustain * n->adsr.current / 256.0f;
        }
    }
    if (target == ADSR_DECAY)
        list_front(p, n, LIST_DECAYING);
}
static void layer_note_decay(PortableN64Player *p, int16_t layer) { layer_decay_release(p, layer, ADSR_DECAY); }
static void layer_note_release(PortableN64Player *p, int16_t layer) { layer_decay_release(p, layer, ADSR_RELEASE); }

static void release_and_take_ownership(PortableN64Player *p, int note, int16_t layer)
{
    PortableN64Note *n = &p->notes[note];
    n->wanted_parent = layer;
    n->priority = PRIORITY_STOPPING;
    n->adsr.fade_out = UPDATE_INVERSE;
    n->adsr.action |= ACTION_RELEASE;
}

/* alloc_note; the player's notes are one pool, so the per-channel and
 * per-sequence reservations collapse into it. */
static int alloc_note(PortableN64Player *p, int16_t layer_index)
{
    PortableN64Layer *layer = layer_at(p, layer_index);
    PortableN64Channel *channel = layer_channel(p, layer_index);
    if (channel->alloc_policy & ALLOC_LAYER)
    {
        const int n = layer->note;
        if (n >= 0 && p->notes[n].prev_parent == layer_index && p->notes[n].wanted_parent < 0)
        {
            release_and_take_ownership(p, n, layer_index);
            list_back(p, &p->notes[n], LIST_RELEASING);
            return n;
        }
    }
    int n = list_pop_back(p, LIST_DISABLED);
    if (n >= 0)
    {
        note_init_for_layer(p, n, layer_index);
        list_front(p, &p->notes[n], LIST_ACTIVE);
        return n;
    }
    n = list_pop_back(p, LIST_DECAYING);
    if (n >= 0)
    {
        release_and_take_ownership(p, n, layer_index);
        list_back(p, &p->notes[n], LIST_RELEASING);
        return n;
    }
    /* pop_node_with_lower_prio walks the active list front to back and keeps
     * the last lowest priority. */
    int best = -1;
    for (unsigned i = 0; i < PORTABLE_N64_NOTES; ++i)
    {
        const PortableN64Note *c = &p->notes[i];
        if (c->list != LIST_ACTIVE)
            continue;
        if (best < 0 || c->priority < p->notes[best].priority ||
            (c->priority == p->notes[best].priority && c->order > p->notes[best].order))
            best = (int)i;
    }
    if (best < 0 || channel->priority <= p->notes[best].priority)
    {
        layer->status = 0;
        return -1;
    }
    if (p->notes[best].parent >= 0)
        layer_note_release(p, p->notes[best].parent);
    p->notes[best].wanted_parent = layer_index;
    list_back(p, &p->notes[best], LIST_RELEASING);
    return best;
}

static void process_notes(PortableN64Player *p)
{
    for (unsigned i = 0; i < PORTABLE_N64_NOTES; ++i)
    {
        PortableN64Note *n = &p->notes[i];
        if (n->parent >= 0)
        {
            const PortableN64Layer *layer = layer_at(p, n->parent);
            const PortableN64Channel *c = layer_channel(p, n->parent);
            if ((!layer->enabled && n->priority >= PRIORITY_MIN) ||
                (p->muted && (c->mute_behavior & (MUTE_STOP_SCRIPT | MUTE_STOP_NOTES))))
            {
                layer_note_release(p, n->parent);
                list_front(p, n, LIST_DECAYING);
                n->priority = PRIORITY_STOPPING;
            }
        }
        else if (n->priority >= PRIORITY_MIN)
            continue;
        if (n->priority == PRIORITY_DISABLED)
            continue;
        if (n->priority == PRIORITY_STOPPING || n->finished)
        {
            if (n->adsr.state == ADSR_DISABLED || n->finished)
            {
                if (n->wanted_parent >= 0)
                {
                    const int16_t wanted = n->wanted_parent;
                    note_disable(n);
                    note_init_for_layer(p, (int)i, wanted);
                    vibrato_init(p, (int)i);
                    list_back(p, n, LIST_ACTIVE);
                    n->wanted_parent = -1;
                }
                else
                {
                    note_disable(n);
                    list_back(p, n, LIST_DISABLED);
                    continue;
                }
            }
        }
        else if (n->adsr.state == ADSR_DISABLED)
        {
            note_disable(n);
            list_back(p, n, LIST_DISABLED);
            continue;
        }
        const float scale = adsr_update(p, &n->adsr);
        if (n->portamento.mode)
            n->portamento_scale = portamento_scale(&n->portamento);
        if (n->vibrato.active && n->parent >= 0)
            n->vibrato_scale = vibrato_scale(p, &n->vibrato);
        float frequency, velocity;
        uint8_t pan, reverb;
        if (n->priority == PRIORITY_STOPPING)
        {
            frequency = n->attr_freq_scale;
            velocity = n->attr_velocity;
            pan = n->attr_pan;
            reverb = n->attr_reverb;
        }
        else
        {
            const PortableN64Layer *layer = layer_at(p, n->parent);
            frequency = layer->note_freq_scale;
            velocity = layer->note_velocity;
            pan = layer->note_pan;
            reverb = layer_channel(p, n->parent)->reverb;
        }
        frequency *= n->vibrato_scale * n->portamento_scale;
        set_resampling(p, n, frequency);
        set_vel_pan_reverb(n, velocity * scale, pan, reverb);
    }
}

/* ---- Layers ----------------------------------------------------------------- */

static int bank_sound(PortableN64Player *p, const Instrument *inst, uint32_t semitone, uint16_t *sample, float *tuning)
{
    (void)p;
    const unsigned range = semitone < inst->low ? 0u : semitone <= inst->high ? 1u : 2u;
    *sample = inst->sample[range];
    *tuning = inst->tuning[range];
    return (int)range;
}

static int get_instrument(PortableN64Player *p, uint32_t index, Instrument *inst)
{
    if (index >= p->song.instrument_count)
        return 0;
    read_instrument(&p->song, index, inst);
    return inst->present;
}

static void layer_process(PortableN64Player *p, int16_t index)
{
    PortableN64Layer *layer = layer_at(p, index);
    PortableN64Channel *c = layer_channel(p, index);
    if (!layer->enabled)
        return;
    if (layer->delay > 1u)
    {
        layer->delay--;
        if (!layer->stop_something && layer->delay <= layer->duration)
        {
            layer_note_decay(p, index);
            layer->stop_something = 1;
        }
        return;
    }
    if (!layer->continuous)
        layer_note_decay(p, index);
    if ((layer->portamento.mode & 0x7fu) == 1u || (layer->portamento.mode & 0x7fu) == 2u)
        layer->portamento.mode = 0;
    layer->properties_init = 1;
    PortableN64Script *s = &layer->script;
    uint8_t cmd = 0;
    uint32_t previous_sound = layer->sound;
    for (unsigned guard = 0;; ++guard)
    {
        if (guard >= SCRIPT_LIMIT)
        {
            fail(p, PORTABLE_N64_RUNAWAY_SCRIPT, s->pc);
            return;
        }
        if (p->status)
            return;
        cmd = rd8(p, s);
        if (cmd <= 0xc0u)
            break;
        uint32_t v;
        switch (cmd)
        {
        case 0xff:
            if (s->depth == 0u)
            {
                layer_note_decay(p, index);
                layer->enabled = 0;
                layer->finished = 1;
                return;
            }
            s->pc = s->stack[--s->depth];
            break;
        case 0xfc:
            v = rd16(p, s);
            push(p, s, s->pc);
            s->pc = v;
            break;
        case 0xf8:
            v = rd8(p, s);
            if (s->depth < PORTABLE_N64_STACK)
                s->loops[s->depth] = (uint8_t)v;
            push(p, s, s->pc);
            break;
        case 0xf7:
            if (!s->depth)
                {
    fail(p, PORTABLE_N64_BAD_COMMAND, s->pc - 1u);
    return;
}
            if (--s->loops[s->depth - 1u] != 0u)
                s->pc = s->stack[s->depth - 1u];
            else
                s->depth--;
            break;
        case 0xfb:
            s->pc = rd16(p, s);
            break;
        case 0xf4:
            v = rd8(p, s);
            s->pc = (uint32_t)((int32_t)s->pc + (int8_t)v);
            break;
        case 0xc1:
            v = rd8(p, s);
            layer->velocity_square = (float)(v * v) / 16129.0f;
            break;
        case 0xca:
            layer->pan = rd8(p, s);
            break;
        case 0xc2:
            layer->transposition = (int8_t)rd8(p, s);
            break;
        case 0xc9:
            layer->note_duration = rd8(p, s);
            break;
        case 0xc4:
        case 0xc5:
            layer->continuous = cmd == 0xc4u;
            layer_note_decay(p, index);
            break;
        case 0xc3:
            layer->short_default = rdvar(p, s);
            break;
        case 0xc6:
        {
            const uint8_t id = rd8(p, s);
            if (id >= 0x7fu)
            {
                if (id == 0x7fu)
                    layer->inst_or_wave = 0;
                else
                {
                    layer->inst_or_wave = id;
                    layer->instrument = -1;
                }
                if (id == 0xffu)
                    layer->adsr.release = 0;
                break;
            }
            Instrument inst;
            if (get_instrument(p, id, &inst))
            {
                layer->adsr.envelope = inst.envelope;
                layer->adsr.release = inst.release;
                layer->instrument = id;
                layer->inst_or_wave = (uint8_t)(id + 1u);
            }
            else
            {
                layer->instrument = -1;
                layer->inst_or_wave = 0xff;
            }
            break;
        }
        case 0xc7:
        {
            layer->portamento.mode = rd8(p, s);
            uint8_t note = (uint8_t)(rd8(p, s) + c->transposition + layer->transposition + p->transposition);
            if (note >= 0x80u)
                note = 0;
            layer->portamento_target = note;
            if (layer->portamento.mode & 0x80u)
                layer->portamento_time = rd8(p, s);
            else
                layer->portamento_time = rdvar(p, s);
            break;
        }
        case 0xc8:
            layer->portamento.mode = 0;
            break;
        case 0xcb:
            v = rd16(p, s);
            layer->adsr.envelope = p->song.sequence_offset + v;
            layer->adsr.release = rd8(p, s);
            break;
        case 0xcc:
            layer->ignore_drum_pan = 1;
            break;
        default:
            if ((cmd & 0xf0u) == 0xd0u)
            {
                v = p->short_velocity ? seq_byte(p, p->short_velocity + (cmd & 15u)) : default_velocity[cmd & 15u];
                layer->velocity_square = (float)(v * v) / 16129.0f;
            }
            else if ((cmd & 0xf0u) == 0xe0u)
                layer->note_duration =
                    p->short_duration ? seq_byte(p, p->short_duration + (cmd & 15u)) : default_duration[cmd & 15u];
            break;
        }
    }
    if (p->status)
        return;
    int same_sound = 1;
    if (cmd == 0xc0u)
    {
        layer->delay = rdvar(p, s);
        layer->stop_something = 1;
    }
    else
    {
        layer->stop_something = 0;
        uint32_t length;
        if (c->large_notes)
        {
            int32_t velocity = 0;
            switch (cmd & 0xc0u)
            {
            case 0x00:
                length = rdvar(p, s);
                velocity = rd8(p, s);
                layer->note_duration = rd8(p, s);
                layer->play_percentage = (uint16_t)length;
                break;
            case 0x40:
                length = rdvar(p, s);
                velocity = rd8(p, s);
                layer->note_duration = 0;
                layer->play_percentage = (uint16_t)length;
                break;
            default:
                length = layer->play_percentage;
                velocity = rd8(p, s);
                layer->note_duration = rd8(p, s);
                break;
            }
            if (velocity >= 0x80 || velocity < 0)
                velocity = 0x7f;
            layer->velocity_square = (float)velocity * (float)velocity / 16129.0f;
        }
        else
        {
            switch (cmd & 0xc0u)
            {
            case 0x00:
                length = rdvar(p, s);
                layer->play_percentage = (uint16_t)length;
                break;
            case 0x40:
                length = layer->short_default;
                break;
            default:
                length = layer->play_percentage;
                break;
            }
        }
        cmd = (uint8_t)(cmd - (cmd & 0xc0u));
        layer->delay = (uint16_t)length;
        layer->duration = (uint16_t)(layer->note_duration * length >> 8);
        if (p->muted && (c->mute_behavior & MUTE_STOP_NOTES))
            layer->stop_something = 1;
        else
        {
            uint32_t kind = layer->inst_or_wave;
            if (kind == 0xffu)
            {
                if (!c->has_instrument)
                    return;
                kind = c->inst_or_wave;
            }
            if (kind == 0u)
            {
                const uint8_t drum_index = (uint8_t)(cmd + c->transposition + layer->transposition);
                Drum drum = {0};
                if (drum_index < p->song.drum_count)
                    read_drum(&p->song, drum_index, &drum);
                if (!drum.present)
                {
                    layer->stop_something = 1;
                    return;
                }
                layer->adsr.envelope = drum.envelope;
                layer->adsr.release = drum.release;
                if (!layer->ignore_drum_pan)
                    layer->pan = drum.pan;
                layer->sample = drum.sample;
                layer->tuning = drum.tuning;
                layer->sound = 0x10000u | drum_index;
                layer->freq_scale = drum.tuning;
            }
            else
            {
                const uint8_t semitone = (uint8_t)(cmd + p->transposition + c->transposition + layer->transposition);
                if (semitone >= 0x80u)
                    layer->stop_something = 1;
                else
                {
                    Instrument inst;
                    const int16_t which = layer->inst_or_wave == 0xffu ? c->instrument : layer->instrument;
                    const int have = which >= 0 && get_instrument(p, (uint32_t)which, &inst);
                    if (layer->portamento.mode)
                    {
                        const uint32_t key = layer->portamento_target < semitone ? semitone : layer->portamento_target;
                        float tuning = 1.0f;
                        if (have)
                        {
                            const int range = bank_sound(p, &inst, key, &layer->sample, &tuning);
                            const uint32_t sound = (uint32_t)which << 2 | (uint32_t)range;
                            same_sound = sound == previous_sound;
                            layer->sound = sound;
                            layer->tuning = tuning;
                        }
                        else
                        {
                            layer->sound = NO_SOUND;
                            layer->sample = 0xffff;
                        }
                        const float from = note_frequency(semitone) * tuning;
                        const float to = note_frequency(layer->portamento_target) * tuning;
                        const uint32_t mode = layer->portamento.mode & 0x7fu;
                        const float freq = (mode == 1u || mode == 3u || mode == 5u) ? to : from;
                        layer->portamento.extent = from / freq - 1.0f;
                        const float time = layer->portamento_time ? (float)layer->portamento_time : 1.0f;
                        if (layer->portamento.mode & 0x80u)
                            layer->portamento.speed = 32512.0f * (float)p->tempo /
                                                      ((float)(layer->delay ? layer->delay : 1u) *
                                                       (float)TEMPO_LIMIT * time);
                        else
                            layer->portamento.speed = 127.0f / time;
                        layer->portamento.cur = 0.0f;
                        layer->freq_scale = freq;
                        if (mode == 5u)
                            layer->portamento_target = semitone;
                    }
                    else if (have)
                    {
                        float tuning;
                        const int range = bank_sound(p, &inst, semitone, &layer->sample, &tuning);
                        const uint32_t sound = (uint32_t)which << 2 | (uint32_t)range;
                        same_sound = sound == previous_sound;
                        layer->sound = sound;
                        layer->tuning = tuning;
                        layer->freq_scale = note_frequency(semitone) * tuning;
                    }
                    else
                    {
                        layer->sound = NO_SOUND;
                        layer->sample = 0xffff;
                        layer->freq_scale = note_frequency(semitone);
                    }
                }
            }
        }
    }
    if (layer->stop_something)
    {
        if (layer->note >= 0 || layer->continuous)
            layer_note_decay(p, index);
        return;
    }
    int allocate = 0;
    if (!layer->continuous || layer->note < 0 || layer->status == 0u)
        allocate = 1;
    else if (!same_sound)
    {
        layer_note_decay(p, index);
        allocate = 1;
    }
    else if (p->notes[layer->note].parent != index)
        allocate = 1;
    else if (layer->sound == NO_SOUND && p->notes[layer->note].synthetic)
    {
        PortableN64Note *n = &p->notes[layer->note];
        const uint32_t before = n->wave_block;
        const uint32_t wave = layer->inst_or_wave == 0xffu ? c->inst_or_wave : layer->inst_or_wave;
        build_synthetic_wave(n, layer, wave);
        n->position = (uint64_t)((n->position >> 16) * wave_block_size[n->wave_block] / wave_block_size[before]) << 16;
    }
    if (allocate)
    {
        const int note = alloc_note(p, index);
        layer->note = (int8_t)note;
    }
    if (layer->note >= 0 && p->notes[layer->note].parent == index)
        vibrato_init(p, layer->note);
}

static void layer_set(PortableN64Player *p, PortableN64Channel *c, int16_t index)
{
    PortableN64Layer *layer = layer_at(p, index);
    if (layer->allocated)
        layer_note_decay(p, index);
    const uint16_t play = layer->play_percentage, short_default = layer->short_default;
    *layer = (PortableN64Layer){0};
    layer->allocated = 1;
    layer->play_percentage = play;
    layer->short_default = short_default;
    layer->adsr = c->adsr;
    layer->adsr.release = 0;
    layer->enabled = 1;
    layer->note_duration = 0x80;
    layer->pan = 0x40;
    layer->note = -1;
    layer->instrument = -1;
    layer->sample = 0xffff;
    layer->sound = NO_SOUND;
    layer->freq_scale = 1.0f;
    layer->inst_or_wave = 0xff;
}

static void layer_free(PortableN64Player *p, int16_t index)
{
    PortableN64Layer *layer = layer_at(p, index);
    if (!layer->allocated)
        return;
    layer_note_decay(p, index);
    layer->enabled = 0;
    layer->finished = 1;
    layer->allocated = 0;
}

/* ---- Channels --------------------------------------------------------------- */

static void channel_init(PortableN64Channel *c, const PortableN64Player *p)
{
    *c = (PortableN64Channel){0};
    c->valid = 1;
    c->changes = 0xff;
    c->new_pan = 0x40;
    c->pan_weight = 0x80;
    c->priority = PRIORITY_DEFAULT;
    c->adsr.envelope = 0u;
    c->adsr.release = 0x20;
    c->vibrato_rate_target = 0x800;
    c->vibrato_rate_start = 0x800;
    c->volume = 1.0f;
    c->volume_scale = 1.0f;
    c->freq_scale = 1.0f;
    c->instrument = -1;
    c->mute_behavior = p->mute_behavior;
    memset(c->io, 0xff, sizeof c->io);
}

static void channel_disable(PortableN64Player *p, unsigned index)
{
    PortableN64Channel *c = &p->channels[index];
    for (unsigned i = 0; i < PORTABLE_N64_LAYERS; ++i)
        layer_free(p, (int16_t)(index * 4u + i));
    c->enabled = 0;
    c->finished = 1;
}

static void channel_enable(PortableN64Player *p, unsigned index, uint32_t pc)
{
    PortableN64Channel *c = &p->channels[index];
    if (!c->valid)
        return;
    c->enabled = 1;
    c->finished = 0;
    c->script.depth = 0;
    c->script.pc = pc;
    c->delay = 0;
    for (unsigned i = 0; i < PORTABLE_N64_LAYERS; ++i)
        layer_free(p, (int16_t)(index * 4u + i));
}

static void init_channels(PortableN64Player *p, uint16_t bits)
{
    for (unsigned i = 0; i < PORTABLE_N64_CHANNELS; ++i, bits >>= 1)
        if (bits & 1u)
        {
            if (p->channels[i].valid)
                channel_disable(p, i);
            channel_init(&p->channels[i], p);
        }
}

static void disable_channels(PortableN64Player *p, uint16_t bits)
{
    for (unsigned i = 0; i < PORTABLE_N64_CHANNELS; ++i, bits >>= 1)
        if ((bits & 1u) && p->channels[i].valid)
        {
            channel_disable(p, i);
            p->channels[i].valid = 0;
        }
}

/* The section holds one bank; a bank-set index other than 0 would select
 * another sequence's bank. */
static int select_bank(PortableN64Player *p, PortableN64Script *s, uint8_t index)
{
    if (index != 0u)
    {
        fail(p, PORTABLE_N64_BAD_COMMAND, s->pc - 1u);
        return 0;
    }
    return 1;
}

static void set_instrument(PortableN64Player *p, PortableN64Channel *c, uint8_t id)
{
    if (id >= 0x80u)
    {
        c->inst_or_wave = id;
        c->instrument = -1;
    }
    else if (id == 0x7fu)
    {
        c->inst_or_wave = 0;
        c->instrument = -1;
    }
    else
    {
        Instrument inst;
        if (!get_instrument(p, id, &inst))
        {
            c->inst_or_wave = 0;
            c->has_instrument = 0;
            return;
        }
        c->adsr.envelope = inst.envelope;
        c->adsr.release = inst.release;
        c->instrument = id;
        c->inst_or_wave = (uint8_t)(id + 1u);
    }
    c->has_instrument = 1;
}

static void channel_process(PortableN64Player *p, unsigned index)
{
    PortableN64Channel *c = &p->channels[index];
    if (!c->enabled)
        return;
    if (c->stop_script)
    {
        for (unsigned i = 0; i < PORTABLE_N64_LAYERS; ++i)
            if (c->layers[i].allocated)
                layer_process(p, (int16_t)(index * 4u + i));
        return;
    }
    if (p->muted && (c->mute_behavior & MUTE_STOP_SCRIPT))
        return;
    if (c->delay)
        c->delay--;
    PortableN64Script *s = &c->script;
    if (c->delay == 0u)
        for (unsigned guard = 0;; ++guard)
        {
            if (guard >= SCRIPT_LIMIT)
            {
                fail(p, PORTABLE_N64_RUNAWAY_SCRIPT, s->pc);
                return;
            }
            if (p->status)
                return;
            const uint8_t cmd = rd8(p, s);
            uint32_t v;
            int8_t t;
            if (cmd > 0xc0u)
            {
                int done = 0;
                switch (cmd)
                {
                case 0xff:
                    if (s->depth == 0u)
                    {
                        channel_disable(p, index);
                        done = 1;
                    }
                    else
                        s->pc = s->stack[--s->depth];
                    break;
                case 0xfe:
                    done = 1;
                    break;
                case 0xfd:
                    c->delay = rdvar(p, s);
                    done = 1;
                    break;
                case 0xea:
                    c->stop_script = 1;
                    done = 1;
                    break;
                case 0xfc:
                    v = rd16(p, s);
                    push(p, s, s->pc);
                    s->pc = v;
                    break;
                case 0xf8:
                    v = rd8(p, s);
                    if (s->depth < PORTABLE_N64_STACK)
                        s->loops[s->depth] = (uint8_t)v;
                    push(p, s, s->pc);
                    break;
                case 0xf7:
                    if (!s->depth)
                        {
    fail(p, PORTABLE_N64_BAD_COMMAND, s->pc - 1u);
    return;
}
                    if (--s->loops[s->depth - 1u] != 0u)
                        s->pc = s->stack[s->depth - 1u];
                    else
                        s->depth--;
                    break;
                case 0xf6:
                    if (!s->depth)
                        {
    fail(p, PORTABLE_N64_BAD_COMMAND, s->pc - 1u);
    return;
}
                    s->depth--;
                    break;
                case 0xf5:
                case 0xf9:
                case 0xfa:
                case 0xfb:
                    v = rd16(p, s);
                    if ((cmd == 0xfau && c->value != 0) || (cmd == 0xf9u && c->value >= 0) ||
                        (cmd == 0xf5u && c->value < 0))
                        break;
                    s->pc = v;
                    break;
                case 0xf2:
                case 0xf3:
                case 0xf4:
                    t = (int8_t)rd8(p, s);
                    if ((cmd == 0xf3u && c->value != 0) || (cmd == 0xf2u && c->value >= 0))
                        break;
                    s->pc = (uint32_t)((int32_t)s->pc + t);
                    break;
                case 0xf1: /* reserve notes: one pool per player */
                    (void)rd8(p, s);
                    break;
                case 0xf0:
                    break;
                case 0xc2:
                    c->dyn_table = rd16(p, s);
                    break;
                case 0xc5:
                    if (c->value != -1)
                        c->dyn_table = dyn_entry(p, c->dyn_table, c->value);
                    break;
                case 0xeb:
                    if (!select_bank(p, s, rd8(p, s)))
                        return;
                    set_instrument(p, c, rd8(p, s));
                    break;
                case 0xc1:
                    set_instrument(p, c, rd8(p, s));
                    break;
                case 0xc3:
                    c->large_notes = 0;
                    break;
                case 0xc4:
                    c->large_notes = 1;
                    break;
                case 0xdf:
                    c->volume = (float)rd8(p, s) / 127.0f;
                    c->changes |= CHANGE_VOLUME;
                    break;
                case 0xe0:
                    c->volume_scale = (float)rd8(p, s) / 128.0f;
                    c->changes |= CHANGE_VOLUME;
                    break;
                case 0xde:
                    c->freq_scale = (float)(uint16_t)rd16(p, s) / 32768.0f;
                    c->changes |= CHANGE_FREQ;
                    break;
                case 0xd3:
                    c->freq_scale = pitch_bend((uint8_t)(rd8(p, s) + 127u));
                    c->changes |= CHANGE_FREQ;
                    break;
                case 0xdd:
                    c->new_pan = rd8(p, s);
                    c->changes |= CHANGE_PAN;
                    break;
                case 0xdc:
                    c->pan_weight = rd8(p, s);
                    c->changes |= CHANGE_PAN;
                    break;
                case 0xdb:
                    c->transposition = (int8_t)rd8(p, s);
                    break;
                case 0xda:
                    c->adsr.envelope = p->song.sequence_offset + rd16(p, s);
                    break;
                case 0xd9:
                    c->adsr.release = rd8(p, s);
                    break;
                case 0xd8:
                    c->vibrato_extent_target = (uint16_t)(rd8(p, s) * 8u);
                    c->vibrato_extent_start = 0;
                    c->vibrato_extent_delay = 0;
                    break;
                case 0xd7:
                    c->vibrato_rate_start = c->vibrato_rate_target = (uint16_t)(rd8(p, s) * 32u);
                    c->vibrato_rate_delay = 0;
                    break;
                case 0xe2:
                    c->vibrato_extent_start = (uint16_t)(rd8(p, s) * 8u);
                    c->vibrato_extent_target = (uint16_t)(rd8(p, s) * 8u);
                    c->vibrato_extent_delay = (uint16_t)(rd8(p, s) * 16u);
                    break;
                case 0xe1:
                    c->vibrato_rate_start = (uint16_t)(rd8(p, s) << 5);
                    c->vibrato_rate_target = (uint16_t)(rd8(p, s) << 5);
                    c->vibrato_rate_delay = (uint16_t)(rd8(p, s) * 16u);
                    break;
                case 0xe3:
                    c->vibrato_delay = (uint16_t)(rd8(p, s) * 16u);
                    break;
                case 0xd4:
                    c->reverb = rd8(p, s);
                    break;
                case 0xc6:
                    if (!select_bank(p, s, rd8(p, s)))
                        return;
                    break;
                case 0xc7:
                {
                    const uint8_t add = rd8(p, s);
                    v = rd16(p, s);
                    seq_write(p, v, (uint8_t)(c->value + add));
                    break;
                }
                case 0xc8:
                    c->value = (int8_t)(c->value - (int8_t)rd8(p, s));
                    break;
                case 0xc9:
                    c->value = (int8_t)(c->value & (int8_t)rd8(p, s));
                    break;
                case 0xcc:
                    c->value = (int8_t)rd8(p, s);
                    break;
                case 0xca:
                    c->mute_behavior = rd8(p, s);
                    break;
                case 0xcb:
                    v = (uint16_t)(rd16(p, s) + c->value);
                    c->value = (int8_t)seq_byte(p, v);
                    break;
                case 0xd0: /* stereo headset effects: default pan only */
                    (void)rd8(p, s);
                    break;
                case 0xd1:
                    c->alloc_policy = rd8(p, s);
                    break;
                case 0xd2:
                    c->adsr.sustain = rd8(p, s);
                    break;
                case 0xe5: /* one reverb */
                    (void)rd8(p, s);
                    break;
                case 0xe4:
                    if (c->value != -1)
                    {
                        v = dyn_entry(p, c->dyn_table, c->value);
                        push(p, s, s->pc);
                        s->pc = v;
                    }
                    break;
                case 0xe6:
                    c->book_offset = rd8(p, s);
                    break;
                case 0xe7:
                case 0xe8:
                {
                    uint32_t at = cmd == 0xe7u ? rd16(p, s) : 0u;
                    uint8_t b[8];
                    for (unsigned i = 0; i < 8u; ++i)
                        b[i] = cmd == 0xe7u ? seq_byte(p, at + i) : rd8(p, s);
                    c->mute_behavior = b[0];
                    c->alloc_policy = b[1];
                    c->priority = b[2];
                    c->transposition = (int8_t)b[3];
                    c->new_pan = b[4];
                    c->pan_weight = b[5];
                    c->reverb = b[6];
                    c->changes |= CHANGE_PAN;
                    break;
                }
                case 0xec:
                    c->vibrato_extent_target = c->vibrato_extent_start = c->vibrato_extent_delay = 0;
                    c->vibrato_rate_target = c->vibrato_rate_start = c->vibrato_rate_delay = 0;
                    c->freq_scale = 1.0f;
                    break;
                case 0xe9:
                    c->priority = rd8(p, s);
                    break;
                case 0xef:
                    (void)rd16(p, s);
                    (void)rd8(p, s);
                    break;
                default:
                    {
    fail(p, PORTABLE_N64_BAD_COMMAND, s->pc - 1u);
    return;
}
                }
                if (done)
                    break;
            }
            else
            {
                const unsigned low = cmd & 15u;
                PortableN64Channel *other = &p->channels[low];
                switch (cmd & 0xf0u)
                {
                case 0x00:
                    c->value = low < 4u && c->layers[low].allocated ? (int8_t)c->layers[low].finished : -1;
                    break;
                case 0x70:
                    if (low < 8u)
                        c->io[low] = c->value;
                    break;
                case 0x80:
                    if (low < 8u)
                    {
                        c->value = c->io[low];
                        if (low < 4u)
                            c->io[low] = -1;
                    }
                    break;
                case 0x50:
                    if (low < 8u)
                        c->value = (int8_t)(c->value - c->io[low]);
                    break;
                case 0x60:
                    c->delay = (uint16_t)low;
                    goto out;
                case 0x90:
                    v = rd16(p, s);
                    if (low < 4u)
                    {
                        layer_set(p, c, (int16_t)(index * 4u + low));
                        c->layers[low].script.pc = v;
                    }
                    break;
                case 0xa0:
                    if (low < 4u)
                        layer_free(p, (int16_t)(index * 4u + low));
                    break;
                case 0xb0:
                    if (c->value != -1 && low < 4u)
                    {
                        v = dyn_entry(p, c->dyn_table, c->value);
                        layer_set(p, c, (int16_t)(index * 4u + low));
                        c->layers[low].script.pc = v;
                    }
                    break;
                case 0x10:
                    v = rd16(p, s);
                    channel_enable(p, low, v);
                    break;
                case 0x20:
                    if (other->valid)
                        channel_disable(p, low);
                    break;
                case 0x30:
                    v = rd8(p, s);
                    if (v < 8u && other->valid)
                        other->io[v] = c->value;
                    break;
                case 0x40:
                    v = rd8(p, s);
                    c->value = v < 8u && other->valid ? other->io[v] : -1;
                    break;
                default:
                    {
    fail(p, PORTABLE_N64_BAD_COMMAND, s->pc - 1u);
    return;
}
                }
            }
        }
out:
    for (unsigned i = 0; i < PORTABLE_N64_LAYERS; ++i)
        if (c->layers[i].allocated)
            layer_process(p, (int16_t)(index * 4u + i));
}

/* ---- Sequence --------------------------------------------------------------- */

static void sequence_disable(PortableN64Player *p)
{
    disable_channels(p, 0xffffu);
    p->enabled = 0;
}

static void sequence_process(PortableN64Player *p)
{
    if (!p->enabled)
        return;
    if (p->muted && (p->mute_behavior & MUTE_STOP_SCRIPT))
        return;
    p->tempo_acc += (uint32_t)p->tempo * p->tempo_scale / 256u;
    if (p->tempo_acc < TEMPO_LIMIT)
        return;
    p->tempo_acc -= TEMPO_LIMIT;
    if (p->tempo_acc >= TEMPO_LIMIT)
        p->tempo_acc = TEMPO_LIMIT - 1u;
    PortableN64Script *s = &p->script;
    if (p->delay > 1u)
        p->delay--;
    else
    {
        p->recalculate = 1;
        for (unsigned guard = 0;; ++guard)
        {
            if (guard >= SCRIPT_LIMIT)
            {
                fail(p, PORTABLE_N64_RUNAWAY_SCRIPT, s->pc);
                return;
            }
            if (p->status)
                return;
            const uint8_t cmd = rd8(p, s);
            uint32_t v;
            if (cmd == 0xffu)
            {
                if (s->depth == 0u)
                {
                    sequence_disable(p);
                    break;
                }
                s->pc = s->stack[--s->depth];
                continue;
            }
            if (cmd == 0xfdu)
            {
                p->delay = rdvar(p, s);
                break;
            }
            if (cmd == 0xfeu)
            {
                p->delay = 1;
                break;
            }
            if (cmd >= 0xc0u)
            {
                switch (cmd)
                {
                case 0xfc:
                    v = rd16(p, s);
                    push(p, s, s->pc);
                    s->pc = v;
                    break;
                case 0xf8:
                    v = rd8(p, s);
                    if (s->depth < PORTABLE_N64_STACK)
                        s->loops[s->depth] = (uint8_t)v;
                    push(p, s, s->pc);
                    break;
                case 0xf7:
                    if (!s->depth)
                        {
    fail(p, PORTABLE_N64_BAD_COMMAND, s->pc - 1u);
    return;
}
                    if (--s->loops[s->depth - 1u] != 0u)
                        s->pc = s->stack[s->depth - 1u];
                    else
                        s->depth--;
                    break;
                case 0xfb:
                case 0xfa:
                case 0xf9:
                case 0xf5:
                    v = rd16(p, s);
                    if ((cmd == 0xfau && p->value != 0) || (cmd == 0xf9u && p->value >= 0) ||
                        (cmd == 0xf5u && p->value < 0))
                        break;
                    s->pc = v;
                    break;
                case 0xf4:
                case 0xf3:
                case 0xf2:
                    v = rd8(p, s);
                    if ((cmd == 0xf3u && p->value != 0) || (cmd == 0xf2u && p->value >= 0))
                        break;
                    s->pc = (uint32_t)((int32_t)s->pc + (int8_t)v);
                    break;
                case 0xf1:
                    (void)rd8(p, s);
                    break;
                case 0xf0:
                    break;
                case 0xdf:
                    p->transposition = 0;
                    /* fallthrough */
                case 0xde:
                    p->transposition = (int8_t)(p->transposition + (int8_t)rd8(p, s));
                    break;
                case 0xdc:
                case 0xdd:
                {
                    const uint8_t t = rd8(p, s);
                    int32_t tempo = cmd == 0xddu ? (int32_t)t * (int32_t)TEMPO_SCALE
                                                 : (int32_t)p->tempo + (int8_t)t * (int32_t)TEMPO_SCALE;
                    if (tempo > (int32_t)TEMPO_LIMIT)
                        tempo = TEMPO_LIMIT;
                    if ((int16_t)tempo <= 0)
                        tempo = 1;
                    p->tempo = (uint16_t)tempo;
                    break;
                }
                case 0xda:
                {
                    const uint8_t state = rd8(p, s);
                    v = rd16(p, s);
                    if (state == STATE_0 || state == STATE_FADE_OUT)
                    {
                        if (p->state != STATE_2)
                        {
                            p->fade_timer = (uint16_t)v;
                            p->state = state;
                        }
                    }
                    else if (state == STATE_2)
                    {
                        p->fade_frames = (uint16_t)v;
                        p->state = state;
                        p->fade_velocity = v ? (0.0f - p->fade_volume) / (float)v : 0.0f;
                    }
                    break;
                }
                case 0xdb:
                    v = rd8(p, s);
                    if (p->state == STATE_2)
                        break;
                    if (p->state == STATE_FADE_OUT)
                    {
                        p->state = STATE_0;
                        p->fade_volume = 0.0f;
                    }
                    p->fade_frames = p->fade_timer;
                    if (p->fade_timer)
                        p->fade_velocity = ((float)v / 127.0f - p->fade_volume) / (float)p->fade_frames;
                    else
                        p->fade_volume = (float)v / 127.0f;
                    break;
                case 0xd9:
                    p->fade_scale = (float)(int8_t)rd8(p, s) / 127.0f;
                    break;
                case 0xd7:
                    init_channels(p, rd16(p, s));
                    break;
                case 0xd6:
                    disable_channels(p, rd16(p, s));
                    break;
                case 0xd5:
                    p->mute_scale = (float)(int8_t)rd8(p, s) / 127.0f;
                    break;
                case 0xd4:
                    p->muted = 1;
                    break;
                case 0xd3:
                    p->mute_behavior = rd8(p, s);
                    break;
                case 0xd2:
                    p->short_velocity = rd16(p, s);
                    break;
                case 0xd1:
                    p->short_duration = rd16(p, s);
                    break;
                case 0xd0:
                    (void)rd8(p, s);
                    break;
                case 0xcc:
                    p->value = rd8(p, s);
                    break;
                case 0xc9:
                    p->value &= rd8(p, s);
                    break;
                case 0xc8:
                    p->value -= rd8(p, s);
                    break;
                default:
                    break; /* undefined upper commands are ignored */
                }
            }
            else
            {
                const unsigned low = cmd & 15u;
                switch (cmd & 0xf0u)
                {
                case 0x00:
                    p->value = p->channels[low].finished;
                    break;
                case 0x50:
                    p->value -= p->variation;
                    break;
                case 0x70:
                    p->variation = (int8_t)p->value;
                    break;
                case 0x80:
                    p->value = p->variation;
                    break;
                case 0x90:
                    channel_enable(p, low, rd16(p, s));
                    break;
                default:
                    break;
                }
            }
        }
    }
    for (unsigned i = 0; i < PORTABLE_N64_CHANNELS; ++i)
        if (p->channels[i].valid)
            channel_process(p, i);
}

static void channel_sound(PortableN64Player *p, unsigned index, int recalculate)
{
    PortableN64Channel *c = &p->channels[index];
    if ((c->changes & CHANGE_VOLUME) || recalculate)
    {
        float volume = c->volume * c->volume_scale * p->applied_fade;
        if (p->muted && (c->mute_behavior & MUTE_SOFTEN))
            volume *= p->mute_scale;
        c->applied_volume = volume * volume;
    }
    if (c->changes & CHANGE_PAN)
        c->pan = (uint16_t)(c->new_pan * c->pan_weight);
    for (unsigned i = 0; i < PORTABLE_N64_LAYERS; ++i)
    {
        PortableN64Layer *layer = &c->layers[i];
        if (!layer->allocated || !layer->enabled || layer->note < 0)
            continue;
        const uint8_t pan = (uint8_t)((c->pan + layer->pan * (0x80u - c->pan_weight)) >> 7);
        if (layer->properties_init)
        {
            layer->note_freq_scale = layer->freq_scale * c->freq_scale;
            layer->note_velocity = layer->velocity_square * c->applied_volume;
            layer->note_pan = pan;
            layer->properties_init = 0;
        }
        else
        {
            if (c->changes & CHANGE_FREQ)
                layer->note_freq_scale = layer->freq_scale * c->freq_scale;
            if ((c->changes & CHANGE_VOLUME) || recalculate)
                layer->note_velocity = layer->velocity_square * c->applied_volume;
            if (c->changes & CHANGE_PAN)
                layer->note_pan = pan;
        }
    }
    c->changes = 0;
}

static void sequence_sound(PortableN64Player *p)
{
    if (!p->enabled)
        return;
    if (p->fade_frames)
    {
        p->fade_volume += p->fade_velocity;
        p->recalculate = 1;
        p->fade_volume = p->fade_volume > 1.0f ? 1.0f : p->fade_volume < 0.0f ? 0.0f : p->fade_volume;
        if (--p->fade_frames == 0u && p->state == STATE_2)
        {
            sequence_disable(p);
            return;
        }
    }
    if (p->recalculate)
        p->applied_fade = p->fade_volume * p->fade_scale;
    for (unsigned i = 0; i < PORTABLE_N64_CHANNELS; ++i)
        if (p->channels[i].valid && p->channels[i].enabled)
            channel_sound(p, i, p->recalculate);
    p->recalculate = 0;
}

/* ---- Synthesis -------------------------------------------------------------- */

static int16_t adpcm_sample(PortableN64Player *p, PortableN64Note *n, const Sample *s, uint32_t index)
{
    const uint32_t frame = index >> 4;
    if (n->frame != frame)
    {
        if (n->frame == UINT32_MAX || frame != n->frame + 1u)
        {
            /* Decode from the start (or the loop frame) up to here. */
            int16_t history[2] = {0, 0};
            for (uint32_t f = 0; f <= frame; ++f)
                portable_n64_vadpcm_frame(p->song.bytes + s->data + f * 9u, p->song.bytes + s->book + 8u,
                                          be32(p->song.bytes + s->book + 4u), history, n->decoded);
        }
        else
        {
            int16_t history[2] = {n->decoded[14], n->decoded[15]};
            portable_n64_vadpcm_frame(p->song.bytes + s->data + frame * 9u, p->song.bytes + s->book + 8u,
                                      be32(p->song.bytes + s->book + 4u), history, n->decoded);
        }
        n->frame = frame;
    }
    return n->decoded[index & 15u];
}

/* The next source sample, following the loop; 0 and finished at the end. */
static int16_t next_sample(PortableN64Player *p, PortableN64Note *n, const Sample *s)
{
    if (n->synthetic)
    {
        const int16_t v = synthetic_sample(n->wave, n->wave_block, n->sample_index);
        n->sample_index = (n->sample_index + 1u) & 63u;
        return v;
    }
    if (n->sample_index >= s->loop_end)
    {
        if (!s->loop_count)
        {
            n->ended = 1;
            return 0;
        }
        n->sample_index = s->loop_start;
        n->frame = s->loop_start >> 4;
        for (unsigned i = 0; i < 16u; ++i)
            n->decoded[i] = be16s(p->song.bytes + s->state + i * 2u);
    }
    return adpcm_sample(p, n, s, n->sample_index++);
}

static void synthesize(PortableN64Player *p, int32_t *mix, int32_t *wet, uint32_t first, uint32_t count)
{
    for (unsigned i = 0; i < PORTABLE_N64_NOTES; ++i)
    {
        PortableN64Note *n = &p->notes[i];
        if (!n->enabled)
            continue;
        Sample s = {0};
        if (!n->synthetic)
        {
            if (n->sample >= p->song.sample_count)
            {
                n->finished = 1;
                n->enabled = 0;
                continue;
            }
            read_sample(&p->song, n->sample, &s);
        }
        if (n->needs_init)
        {
            n->needs_init = 0;
            n->volume_left = n->volume_right = 0;
            n->sample_index = 0;
            n->frame = UINT32_MAX;
            n->ended = 0;
            n->position = 0;
            n->previous = 0;
            n->current = next_sample(p, n, &s);
        }
        const int32_t left = n->volume_left, right = n->volume_right;
        const int32_t dl = n->target_left - left, dr = n->target_right - right;
        for (uint32_t k = 0; k < count; ++k)
        {
            if (n->ended)
            {
                n->finished = 1;
                n->enabled = 0;
                break;
            }
            const int32_t frac = (int32_t)(n->position & 0xffffu);
            const int32_t value = n->previous + (int32_t)(((int64_t)(n->current - n->previous) * frac) >> 16);
            const int64_t vl = left + (int64_t)dl * (int32_t)(k + 1u) / (int32_t)count;
            const int64_t vr = right + (int64_t)dr * (int32_t)(k + 1u) / (int32_t)count;
            const int32_t outl = (int32_t)((value * vl) >> 16), outr = (int32_t)((value * vr) >> 16);
            mix[(first + k) * 2u] += outl;
            mix[(first + k) * 2u + 1u] += outr;
            if (n->reverb)
            {
                wet[(first + k) * 2u] += outl * n->reverb >> 7;
                wet[(first + k) * 2u + 1u] += outr * n->reverb >> 7;
            }
            const uint64_t before = n->position >> 16;
            n->position += n->step;
            for (uint64_t a = before; a < (n->position >> 16); ++a)
            {
                n->previous = n->current;
                n->current = next_sample(p, n, &s);
            }
        }
        n->volume_left = n->target_left;
        n->volume_right = n->target_right;
    }
}

/* ---- Player ----------------------------------------------------------------- */

static void reset(PortableN64Player *p, const PortableN64SequenceView *song)
{
    memset(p, 0, sizeof *p);
    p->song = *song;
    p->tempo = 120u * TEMPO_SCALE;
    p->state = STATE_FADE_OUT;
    p->fade_volume = 1.0f;
    p->fade_scale = 1.0f;
    p->mute_scale = 0.5f;
    p->mute_behavior = MUTE_STOP_SCRIPT | MUTE_STOP_NOTES | MUTE_SOFTEN;
    p->variation = song->variation;
    p->tempo_scale = 256u;
    p->volume = 256u;
    p->effect_channel = 0xff;
    for (unsigned i = 0; i < PORTABLE_N64_NOTES; ++i)
    {
        PortableN64Note *n = &p->notes[i];
        n->parent = n->prev_parent = n->wanted_parent = -1;
        list_back(p, n, LIST_DISABLED);
    }
    p->enabled = 1;
    p->active = 1;
}

void portable_n64_player_start(PortableN64Player *player, const PortableN64SequenceView *song)
{
    if (!player || !song || !song->bytes)
        return;
    reset(player, song);
}

PortableN64Status portable_n64_player_start_effect(PortableN64Player *player, const PortableN64SequenceView *song,
                                                   uint8_t bank, uint8_t sound)
{
    if (!player || !song || !song->bytes || bank >= PORTABLE_N64_SOUND_BANKS || !song->effect_channels[bank] ||
        sound >= 0x80u)
        return PORTABLE_N64_INVALID_ARGUMENT;
    reset(player, song);
    player->effect_channel = song->effect_channels[bank] >> 4;
    player->effect_sound = sound;
    player->effect_pending = 1;
    return PORTABLE_N64_OK;
}

void portable_n64_player_stop(PortableN64Player *player)
{
    if (player)
    {
        player->enabled = 0;
        player->active = 0;
    }
}

int portable_n64_player_playing(const PortableN64Player *player) { return player && player->active; }

PortableN64Status portable_n64_player_mix(PortableN64Player *p, int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 2u])
{
    if (!p || !output)
        return PORTABLE_N64_INVALID_ARGUMENT;
    if (p->status || !p->active || p->paused)
        return p->status;
    int32_t mix[PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 2u] = {0}, wet[PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 2u] = {0};
    for (uint32_t u = 0; u < UPDATES; ++u)
    {
        if (p->effect_pending && p->channels[p->effect_channel].valid && p->channels[p->effect_channel].enabled)
        {
            PortableN64Channel *c = &p->channels[p->effect_channel];
            c->io[0] = 1;
            c->io[4] = (int8_t)p->effect_sound;
            c->io[3] = 127;
            p->effect_pending = 0;
        }
        sequence_process(p);
        sequence_sound(p);
        process_notes(p);
        if (p->status)
            return p->status;
        /* The sound manager frees a finished effect's channel. */
        if (p->enabled && p->effect_channel != 0xffu && !p->effect_pending &&
            (uint8_t)p->channels[p->effect_channel].io[7] == 0x80u)
            sequence_disable(p);
        const uint32_t first = PORTABLE_AUDIO_PCM_FRAME_SAMPLES * u / UPDATES;
        const uint32_t last = PORTABLE_AUDIO_PCM_FRAME_SAMPLES * (u + 1u) / UPDATES;
        synthesize(p, mix, wet, first, last - first);
    }
    for (uint32_t i = 0; i < PORTABLE_AUDIO_PCM_FRAME_SAMPLES; ++i)
    {
        const int32_t rl = p->reverb_left[p->reverb_at], rr = p->reverb_right[p->reverb_at];
        p->reverb_left[p->reverb_at] = clamp16(wet[i * 2u] + (int32_t)((float)rl * REVERB_GAIN));
        p->reverb_right[p->reverb_at] = clamp16(wet[i * 2u + 1u] + (int32_t)((float)rr * REVERB_GAIN));
        p->reverb_at = (uint16_t)((p->reverb_at + 1u) % PORTABLE_N64_REVERB_SAMPLES);
        const int32_t l = (mix[i * 2u] + rl) * (int32_t)p->volume >> 8;
        const int32_t r = (mix[i * 2u + 1u] + rr) * (int32_t)p->volume >> 8;
        output[i * 2u] = clamp16(output[i * 2u] + l);
        output[i * 2u + 1u] = clamp16(output[i * 2u + 1u] + r);
    }
    if (!p->enabled)
    {
        int sounding = 0;
        for (unsigned i = 0; i < PORTABLE_N64_NOTES && !sounding; ++i)
            sounding = p->notes[i].priority != PRIORITY_DISABLED || p->notes[i].enabled;
        if (!sounding)
            p->active = 0;
    }
    return PORTABLE_N64_OK;
}

const char *portable_n64_status_name(PortableN64Status status)
{
    static const char *const names[] = {"ok", "invalid argument", "bad section", "bad command", "bad address",
                                        "runaway script"};
    return (unsigned)status < sizeof names / sizeof names[0] ? names[status] : "unknown status";
}
