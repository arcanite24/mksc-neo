#include "rom_import_audio.h"
#include "../audio/audio_tuning.h"
#include "../core/character_audio.h"

#include <string.h>

/* Port of tools/portable/audio_rom.py (RomAudio). Sequences and voice banks
 * live in rom_import_audio_sequence.c and rom_import_audio_bank.c. */

enum {
    SONG_TABLE = 0x102498, /* gSongTable, US revision 0 */
    SONG_COUNT = 403,
    TONE_SIZE = 12,
    TONE_LIMIT = 128,
};

typedef struct Song {
    uint32_t song_id;
    uint32_t header;
    uint32_t bank;
} Song;

static uint32_t *u32_items(const RiBuf *buffer)
{
    return (uint32_t *)(void *)buffer->data;
}

static size_t u32_count(const RiBuf *buffer)
{
    return buffer->size / sizeof(uint32_t);
}

/* bisect.bisect_right over a sorted uint32_t list. */
static size_t bisect_right(const RiBuf *sorted, uint32_t value)
{
    const uint32_t *items = u32_items(sorted);
    size_t low = 0, high = u32_count(sorted);
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (value < items[middle]) high = middle;
        else low = middle + 1;
    }
    return low;
}

static int contains(const RiBuf *sorted, uint32_t value)
{
    size_t index = bisect_right(sorted, value);
    return index > 0 && u32_items(sorted)[index - 1] == value;
}

/* set.add on a list kept sorted and unique. */
static void sorted_add(RiCtx *ctx, RiBuf *sorted, uint32_t value)
{
    if (contains(sorted, value)) return;
    size_t index = bisect_right(sorted, value), count = u32_count(sorted);
    ri_resize(ctx, sorted, (count + 1) * sizeof(uint32_t));
    uint32_t *items = u32_items(sorted);
    memmove(&items[index + 1], &items[index], (count - index) * sizeof(uint32_t));
    items[index] = value;
}

/* min(b for b in limits if b > address), default fallback */
static int64_t next_above(const RiBuf *sorted, uint32_t address, int64_t fallback)
{
    size_t index = bisect_right(sorted, address);
    return index < u32_count(sorted) ? (int64_t)u32_items(sorted)[index] : fallback;
}

static int64_t floor_div(int64_t value, int64_t divisor)
{
    int64_t quotient = value / divisor;
    if ((value % divisor != 0) && ((value < 0) != (divisor < 0))) --quotient;
    return quotient;
}

static int64_t min64(int64_t a, int64_t b)
{
    return a < b ? a : b;
}

int64_t ri_audio_tone_count(RiAudio *audio, uint32_t address)
{
    int64_t end;
    if (contains(audio->banks, address)) end = next_above(audio->banks, address, SONG_TABLE);
    else end = next_above(audio->boundaries, address, (int64_t)audio->ctx->rom.size);
    return min64(TONE_LIMIT, floor_div(end - address, TONE_SIZE));
}

uint32_t ri_audio_sample_id(RiAudio *audio, uint32_t kind, uint32_t address)
{
    const RiAudioSample *samples = (const RiAudioSample *)(const void *)audio->samples->data;
    size_t total = audio->samples->size / sizeof(RiAudioSample);
    uint32_t count = 0;
    for (size_t index = 0; index < total; ++index) {
        if (samples[index].kind == kind && samples[index].address == address) return samples[index].id;
        if (samples[index].kind == kind) ++count;
    }
    if (count >= 0x7FFF) ri_fail(audio->ctx, "too many audio samples");
    RiAudioSample sample = {kind, address, (kind == RI_VOICE_PSG_WAVE ? 0x8000u : 1u) + count};
    ri_put(audio->ctx, audio->samples, &sample, sizeof sample);
    return sample.id;
}

static void put_rom(RiCtx *ctx, RiBuf *out, size_t offset, size_t size)
{
    ri_put(ctx, out, ri_at(ctx, ctx->rom, offset, size), size);
}

/* RomAudio.engine_sound_rules */
static RiBuf *engine_sound_rules(RiCtx *ctx)
{
    RiBuf *out = ri_buf(ctx);
    ri_put(ctx, out, "ENG1", 4);
    static const uint32_t fields[6] = {3, 8, 16, 4, 6, 4};
    for (int index = 0; index < 6; ++index)
        ri_u16(ctx, out, fields[index]);
    put_rom(ctx, out, 0xEC94C, 36);
    for (size_t index = 0; index < 8 * 16; ++index)
        put_rom(ctx, out, 0xEC970 + index * 4, 2);
    put_rom(ctx, out, 0xECB88, 4 * 8 * 2);
    put_rom(ctx, out, 0xECB70, 4 * 2);
    return out;
}

/* RomAudio.hazard_sound_rules */
static void character_audio_rules(RiCtx *ctx,RiBuf *out)
{
    PortableCharacterAudioRules rules={0};
    unsigned used=0;
    for(unsigned i=0;i<8u;++i){
        const unsigned slot=ri_get_u8(ctx,ctx->rom,0xd92bcu+i);
        if(slot>=9u||(used&(1u<<slot)))ri_fail(ctx,"invalid character audio slot map");
        used|=1u<<slot;
        rules.lock_voices[i]=(uint16_t)ri_get_u16(ctx,ctx->rom,0xd92c4u+slot*2u);
        ri_u16(ctx,out,rules.lock_voices[i]);
    }
    for (unsigned table = 0; table < 2u; ++table)
        for (unsigned i = 0; i < 8u; ++i)
        {
            PortableCharacterRevCue *cue = table ? &rules.launch_revs[i] : &rules.menu_revs[i];
            const size_t address = (table ? 0xd95fcu : 0xd92fau) + i * 4u;
            cue->song = (uint16_t)ri_get_u16(ctx, ctx->rom, address);
            cue->volume = (uint16_t)ri_get_u16(ctx, ctx->rom, address + 2u);
            ri_u16(ctx, out, cue->song);
            ri_u16(ctx, out, cue->volume);
        }
    for(unsigned i=0;i<9u;++i){
        rules.menu_horns[i]=(uint16_t)ri_get_u16(ctx,ctx->rom,0xd92e8u+i*2u);
        ri_u16(ctx,out,rules.menu_horns[i]);
    }
    for(unsigned i=0;i<8u;++i){
        rules.race_horns[i]=(uint16_t)ri_get_u16(ctx,ctx->rom,0xecb78u+i*2u);
        ri_u16(ctx,out,rules.race_horns[i]);
    }
    for(unsigned i=0;i<8u;++i){
        const uint32_t song=ri_get_u32(ctx,ctx->rom,0xdbc48u+i*4u);
        if(song>=403u)ri_fail(ctx,"invalid character audio cue");
        rules.ceremony_voices[i]=(uint16_t)song;ri_u16(ctx,out,song);
    }
    if(!portable_character_audio_rules_valid(&rules))ri_fail(ctx,"invalid character audio cue");
}

static RiBuf *hazard_sound_rules(RiCtx *ctx)
{
    RiData voices = ri_span(ctx, ctx->rom, 0xECC08, 64), impacts = ri_span(ctx, ctx->rom, 0xECBC8, 64);
    for (size_t index = 0; index < 32; ++index) {
        uint32_t voice = ri_le16(voices.data + index * 2), impact = ri_le16(impacts.data + index * 2);
        if (voice == 0 || voice >= 403 || impact == 0 || impact >= 403) ri_fail(ctx, "invalid hazard voice song");
    }
    RiBuf *out = ri_buf(ctx);
    ri_put(ctx, out, "HAZ1", 4);
    static const uint32_t fields[4] = {4, 8, 2, 2};
    for (int index = 0; index < 4; ++index)
        ri_u16(ctx, out, fields[index]);
    ri_put(ctx, out, voices.data, voices.size);
    ri_put(ctx, out, impacts.data, impacts.size);
    static const size_t reaction_offsets[2]={0xECC88u,0xECC68u};
    for(unsigned table=0u;table<2u;++table){
        const RiData songs=ri_span(ctx,ctx->rom,reaction_offsets[table],32u);
        for(unsigned i=0u;i<16u;++i){
            const uint32_t song=ri_le16(songs.data+i*2u);
            if(song==0u||song>=403u)ri_fail(ctx,"invalid reaction voice song");
        }
        ri_put(ctx,out,songs.data,songs.size);
    }
    character_audio_rules(ctx,out);
    return out;
}

/* RomAudio.finish_sound_rules */
static RiBuf *finish_sound_rules(RiCtx *ctx)
{
    static const size_t scripts[4] = {0xEE5E4, 0xEE604, 0xEE624, 0xEE644};
    RiData rom = ctx->rom;
    RiBuf *out = ri_buf(ctx);
    ri_put(ctx, out, "FIN1", 4);
    ri_u16(ctx, out, 2);
    ri_u16(ctx, out, 4);
    for (int index = 0; index < 4; ++index) {
        size_t address = scripts[index];
        uint32_t cue = 0;
        if (ri_get_u32(ctx, rom, address) == 0x81) {
            cue = ri_get_u32(ctx, rom, address + 4);
            address += 8;
        }
        uint32_t wait = ri_get_u32(ctx, rom, address), delay = ri_get_u32(ctx, rom, address + 4);
        uint32_t start = ri_get_u32(ctx, rom, address + 8), music = ri_get_u32(ctx, rom, address + 12);
        uint32_t end = ri_get_u32(ctx, rom, address + 16), unused = ri_get_u32(ctx, rom, address + 20);
        if (wait != 0xF1 || start != 0x85 || end != 0 || unused != 0 || delay == 0 || delay > 65535 ||
            cue >= 403 || music == 0 || music >= 403)
            ri_fail(ctx, "invalid finish sound script");
        ri_u16(ctx, out, cue);
        ri_u16(ctx, out, delay);
        ri_u16(ctx, out, music);
    }
    /* C38C Battle's two MOV immediate song requests. */
    uint32_t songs[2] = {ri_get_u16(ctx, rom, 0x4C558), ri_get_u16(ctx, rom, 0x4C560)};
    for (int index = 0; index < 2; ++index)
        if ((songs[index] & 0xFF00) != 0x2000 || !(songs[index] & 255))
            ri_fail(ctx, "invalid Battle finish song instruction");
    uint32_t lap = ri_get_u16(ctx, rom, 0x3B950);
    uint32_t script[8];
    for (int index = 0; index < 8; ++index)
        script[index] = ri_get_u32(ctx, rom, 0xEC790 + (size_t)index * 4);
    if ((lap & 0xFF00) != 0x2000 || (lap & 255) == 0 || (lap & 255) >= 403)
        ri_fail(ctx, "invalid lap sound instruction");
    if (script[0] != 0xF4 || script[1] != 0x803DDCD || script[2] != 0xF1 || script[4] != 0xF4 ||
        script[5] != 0x803DDD9 || script[6] != 0 || script[7] != 0 || script[3] == 0 || script[3] > 65535)
        ri_fail(ctx, "invalid final lap sound script");
    ri_u16(ctx, out, songs[0] & 255);
    ri_u16(ctx, out, songs[1] & 255);
    ri_u16(ctx, out, lap & 255);
    ri_u16(ctx, out, script[3]);
    return out;
}

/* RomAudio.player_rules: gMPlayTable priority protection flags. */
static RiBuf *player_rules(RiCtx *ctx)
{
    RiBuf *out = ri_buf(ctx);
    ri_put(ctx, out, "MKSCPRI1", 8);
    ri_u16(ctx, out, 2);
    ri_u16(ctx, out, 11);
    for (size_t player = 0; player < 11; ++player) {
        uint32_t flag = ri_get_u16(ctx, ctx->rom, 0x102414 + player * 12 + 10);
        if (flag > 1) ri_fail(ctx, "invalid music player priority flag");
        ri_u8(ctx, out, flag);
    }
    ri_put(ctx,out,ri_at(ctx,ctx->rom,0xf3314u,48u),48u);
    ri_put(ctx,out,ri_at(ctx,ctx->rom,0xf33e0u,24u),24u);
    ri_put(ctx,out,ri_at(ctx,ctx->rom,0xf33f8u,60u),60u);
    PortableAudioTuning tuning;
    if(!portable_audio_tuning_open(&tuning,out->data+23u,out->size-23u))
        ri_fail(ctx,"invalid audio tuning tables");
    return out;
}

/* RomAudio.__init__ boundary scan: ToneData tables may be shorter than 128
 * entries; child tables, key maps and wave data also terminate tables. The
 * pending list is a stack and boundaries grow during the scan, so the visit
 * order matters and is kept. */
static void scan_boundaries(RiAudio *audio)
{
    RiCtx *ctx = audio->ctx;
    RiBuf *limits_for_banks = ri_buf(ctx); /* self.banks + [SONG_TABLE] */
    ri_put_buf(ctx, limits_for_banks, audio->banks);
    sorted_add(ctx, limits_for_banks, SONG_TABLE);
    RiBuf *pending = ri_buf(ctx);
    ri_put_buf(ctx, pending, audio->banks);
    RiBuf *visited = ri_buf(ctx); /* sorted set */
    while (pending->size) {
        uint32_t address = u32_items(pending)[u32_count(pending) - 1];
        ri_resize(ctx, pending, pending->size - sizeof(uint32_t));
        if (contains(visited, address)) continue;
        sorted_add(ctx, visited, address);
        int is_bank = contains(audio->banks, address);
        for (uint32_t index = 0; index < TONE_LIMIT; ++index) {
            size_t offset = (size_t)address + index * TONE_SIZE;
            const RiBuf *limits = is_bank ? limits_for_banks : audio->boundaries;
            int64_t end = next_above(limits, address, (int64_t)ctx->rom.size);
            if ((int64_t)offset + TONE_SIZE > min64(end, (int64_t)address + TONE_LIMIT * TONE_SIZE)) break;
            uint32_t tone_type = ri_at(ctx, ctx->rom, offset, TONE_SIZE)[0];
            if (tone_type & 0xC0) {
                uint32_t child = (uint32_t)ri_pointer(ctx, ctx->rom, offset + 4);
                sorted_add(ctx, audio->boundaries, child);
                ri_put(ctx, pending, &child, sizeof child);
                if (!(tone_type & 0x80))
                    sorted_add(ctx, audio->boundaries, (uint32_t)ri_pointer(ctx, ctx->rom, offset + 8));
            } else if ((tone_type & 7) == 0 || (tone_type & 7) == 3) {
                sorted_add(ctx, audio->boundaries, (uint32_t)ri_pointer(ctx, ctx->rom, offset + 4));
            }
        }
    }
    ri_buf_free(ctx, visited);
    ri_buf_free(ctx, pending);
    ri_buf_free(ctx, limits_for_banks);
}

/* normalize_pcm / normalize_psg via build_asset (export_audio_sample.py). */
static RiBuf *sample_asset(RiCtx *ctx, const RiAudioSample *sample)
{
    enum { SAMPLE_HEADER = 40, PCM_S8 = 1, PSG_WAVE4 = 2, LOOP = 1 };
    RiData raw;
    uint32_t encoding, flags = 0, frequency = 0, count, loop_start = 0;
    if (sample->kind == RI_VOICE_PCM) {
        uint32_t size = ri_get_u32(ctx, ctx->rom, (size_t)sample->address + 12);
        if ((uint64_t)sample->address + 17 + size > ctx->rom.size)
            ri_fail(ctx, "audio data outside ROM at 0x%x", (unsigned)sample->address);
        raw = ri_span(ctx, ctx->rom, sample->address, (size_t)17 + size);
        uint32_t wave_type = ri_le16(raw.data), status = ri_le16(raw.data + 2);
        frequency = ri_le32(raw.data + 4);
        loop_start = ri_le32(raw.data + 8);
        count = size + 1; /* stored_size + 1 == len(payload) by construction */
        if (wave_type != 0 || (status & ~0x4000u)) ri_fail(ctx, "unsupported PCM wave type or status");
        if (frequency < 1024) ri_fail(ctx, "PCM source rate is zero");
        flags = (status & 0x4000) ? LOOP : 0;
        if (flags && loop_start >= count) ri_fail(ctx, "PCM loop start exceeds payload");
        if (!flags && loop_start != 0) ri_fail(ctx, "non-looping PCM wave has a loop start");
        encoding = PCM_S8;
    } else {
        raw = ri_span(ctx, ctx->rom, sample->address, 16);
        encoding = PSG_WAVE4;
        count = 32;
    }
    RiData payload = sample->kind == RI_VOICE_PCM ? ri_span(ctx, raw, 16, raw.size - 16) : raw;
    uint64_t total = SAMPLE_HEADER + (uint64_t)payload.size;
    if (total > 0xFFFFFFFFu || sample->id > 0xFFFFu) ri_fail(ctx, "audio sample field out of range");
    RiBuf *out = ri_buf(ctx);
    ri_put(ctx, out, "MKSCSMP1", 8);
    ri_u16(ctx, out, SAMPLE_HEADER);
    ri_u16(ctx, out, 1);
    ri_u16(ctx, out, sample->id);
    ri_u8(ctx, out, encoding);
    ri_u8(ctx, out, flags);
    ri_words(ctx, out, 6, (uint32_t)frequency, (uint32_t)count, (uint32_t)loop_start,
             (uint32_t)SAMPLE_HEADER, (uint32_t)total, (uint32_t)0);
    ri_put(ctx, out, payload.data, payload.size);
    return out;
}

void ri_group_audio(RiCtx *ctx)
{
    RiData rom = ctx->rom;
    if (rom.size != 0x400000 || memcmp(rom.data + 0xAC, "AMKE", 4) != 0 || rom.data[0xBC] != 0)
        ri_fail(ctx, "audio import requires the 4 MiB US revision 0 ROM");
    RiAudio audio = {ctx, ri_buf(ctx), ri_buf(ctx), ri_buf(ctx)};

    RiBuf *song_buf = ri_buf(ctx);
    for (uint32_t song_id = 1; song_id < SONG_COUNT; ++song_id) {
        size_t header = ri_pointer(ctx, rom, SONG_TABLE + (size_t)song_id * 8);
        if (rom.data[header] == 0) continue; /* Original dummy_song entries have no tracks. */
        Song song = {song_id, (uint32_t)header, (uint32_t)ri_pointer(ctx, rom, header + 4)};
        ri_put(ctx, song_buf, &song, sizeof song);
        sorted_add(ctx, audio.banks, song.bank);
    }
    ri_put_buf(ctx, audio.boundaries, audio.banks);
    sorted_add(ctx, audio.boundaries, SONG_TABLE);
    scan_boundaries(&audio);

    ri_section(ctx, 68, 1, 3, engine_sound_rules(ctx));
    ri_section(ctx, 69, 1, 2, player_rules(ctx));
    ri_section(ctx, 71, 1, 1, hazard_sound_rules(ctx));
    ri_section(ctx, 72, 1, 1, finish_sound_rules(ctx));
    /* bank_ids: 1-based index in sorted bank order; samples are numbered in
     * the order the banks first reference them. */
    for (size_t index = 0; index < u32_count(audio.banks); ++index)
        ri_section(ctx, 17, (uint32_t)index + 1, 1,
                   ri_audio_bank(&audio, u32_items(audio.banks)[index], (uint32_t)index + 1));
    const Song *songs = (const Song *)(const void *)song_buf->data;
    for (size_t index = 0; index < song_buf->size / sizeof(Song); ++index) {
        uint32_t bank_id = (uint32_t)bisect_right(audio.banks, songs[index].bank);
        ri_section(ctx, 16, songs[index].song_id, 2,
                   ri_audio_sequence(ctx, songs[index].header, songs[index].song_id, bank_id));
    }
    /* Section order is irrelevant: ri_build sorts like sorted(sections). */
    const RiAudioSample *samples = (const RiAudioSample *)(const void *)audio.samples->data;
    for (size_t index = 0; index < audio.samples->size / sizeof(RiAudioSample); ++index)
        ri_section(ctx, 18, samples[index].id, 1, sample_asset(ctx, &samples[index]));
}
