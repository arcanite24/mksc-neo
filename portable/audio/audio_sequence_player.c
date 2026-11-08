#include "audio_sequence_player.h"

#include <limits.h>
#include <string.h>

#define NO_EVENT UINT32_MAX

static uint32_t next_for_track(const PortableAudioSequenceView *sequence,
                               uint32_t after, uint8_t track)
{
    PortableAudioSequenceEvent event;
    uint32_t index;
    for (index = after; index < sequence->event_count; ++index) {
        if (portable_audio_sequence_event(sequence, index, &event) !=
            PORTABLE_AUDIO_SEQUENCE_OK)
            return NO_EVENT;
        if (event.track == track)
            return index;
    }
    return NO_EVENT;
}

PortableAudioPlayerStatus portable_audio_sequence_player_init(
    PortableAudioSequencePlayer *player,
    const PortableAudioSequenceView *sequence)
{
    uint8_t track;
    if (player == NULL || sequence == NULL || sequence->data == NULL ||
        sequence->track_count == 0u ||
        sequence->track_count > PORTABLE_AUDIO_SEQUENCE_MAX_TRACKS)
        return PORTABLE_AUDIO_PLAYER_INVALID_ARGUMENT;
    memset(player, 0, sizeof *player);
    player->sequence = sequence;
    player->tempo = 150u;
    for (track = 0u; track < sequence->track_count; ++track) {
        player->tracks[track].cursor = next_for_track(sequence, 0u, track);
        if (player->tracks[track].cursor == NO_EVENT) {
            player->status = PORTABLE_AUDIO_PLAYER_BAD_SEQUENCE;
            return player->status;
        }
        player->tracks[track].active = 1u;
        player->tracks[track].volume = 127u;
        player->tracks[track].pan = 64u;
    }
    return PORTABLE_AUDIO_PLAYER_OK;
}

static PortableAudioPlayerStatus effective_tick(
    const PortableAudioSequencePlayer *player, uint8_t track,
    PortableAudioSequenceEvent *event, uint32_t *tick)
{
    const PortableAudioTrackState *state = &player->tracks[track];
    if (!state->active || state->cursor == NO_EVENT)
        return PORTABLE_AUDIO_PLAYER_BAD_SEQUENCE;
    if (portable_audio_sequence_event(player->sequence, state->cursor, event) !=
        PORTABLE_AUDIO_SEQUENCE_OK)
        return PORTABLE_AUDIO_PLAYER_BAD_SEQUENCE;
    if (UINT32_MAX - state->tick_bias < event->tick)
        return PORTABLE_AUDIO_PLAYER_TICK_OVERFLOW;
    *tick = state->tick_bias + event->tick;
    return PORTABLE_AUDIO_PLAYER_OK;
}

static PortableAudioPlayerStatus apply_event(
    PortableAudioSequencePlayer *player, uint8_t track,
    const PortableAudioSequenceEvent *event)
{
    PortableAudioTrackState *state = &player->tracks[track];
    switch ((PortableAudioSequenceOperation)event->operation) {
    case PORTABLE_AUDIO_EVENT_END:
        state->active = 0u;
        state->cursor = NO_EVENT;
        break;
    case PORTABLE_AUDIO_EVENT_LOOP: {
        PortableAudioSequenceEvent target;
        uint32_t duration;
        if (portable_audio_sequence_event(player->sequence, event->target,
                &target) != PORTABLE_AUDIO_SEQUENCE_OK ||
            target.track != track || target.tick >= event->tick)
            return PORTABLE_AUDIO_PLAYER_BAD_SEQUENCE;
        duration = event->tick - target.tick;
        if (UINT32_MAX - state->tick_bias < duration)
            return PORTABLE_AUDIO_PLAYER_TICK_OVERFLOW;
        state->tick_bias += duration;
        state->cursor = event->target;
        break;
    }
    case PORTABLE_AUDIO_EVENT_TEMPO:
        player->tempo = (uint16_t)(event->value * 2u);
        state->cursor = next_for_track(player->sequence, state->cursor + 1u,
                                       track);
        break;
    case PORTABLE_AUDIO_EVENT_VOICE:
        state->voice = event->value;
        state->cursor = next_for_track(player->sequence, state->cursor + 1u,
                                       track);
        break;
    case PORTABLE_AUDIO_EVENT_VOLUME:
        state->volume = (uint8_t)event->value;
        state->cursor = next_for_track(player->sequence, state->cursor + 1u,
                                       track);
        break;
    case PORTABLE_AUDIO_EVENT_PAN:
        state->pan = (uint8_t)event->value;
        state->cursor = next_for_track(player->sequence, state->cursor + 1u,
                                       track);
        break;
    case PORTABLE_AUDIO_EVENT_NOTE:
    case PORTABLE_AUDIO_EVENT_TIE:
        state->last_key = event->arg0;
        state->last_velocity = event->arg1;
        ++state->note_count;
        state->cursor = next_for_track(player->sequence, state->cursor + 1u,
                                       track);
        break;
    case PORTABLE_AUDIO_EVENT_MEMORY_SET:
        (player->memory_area!=NULL?player->memory_area:player->memory)[event->arg0] = event->arg1;
        state->cursor = next_for_track(player->sequence, state->cursor + 1u,
                                       track);
        break;
    default:
        state->cursor = next_for_track(player->sequence, state->cursor + 1u,
                                       track);
        break;
    }
    if (state->active && state->cursor == NO_EVENT)
        return PORTABLE_AUDIO_PLAYER_BAD_SEQUENCE;
    return PORTABLE_AUDIO_PLAYER_OK;
}

PortableAudioPlayerStatus portable_audio_sequence_player_advance_to(
    PortableAudioSequencePlayer *player, uint32_t target_tick,
    uint32_t event_budget)
{
    return portable_audio_sequence_player_advance_to_sink(player,target_tick,
        event_budget,NULL,NULL);
}

PortableAudioPlayerStatus portable_audio_sequence_player_advance_to_sink(
    PortableAudioSequencePlayer *player,uint32_t target_tick,
    uint32_t event_budget,PortableAudioEventSink sink,void *user)
{
    uint32_t processed = 0u;
    if (player == NULL || player->sequence == NULL)
        return PORTABLE_AUDIO_PLAYER_INVALID_ARGUMENT;
    if (player->status != PORTABLE_AUDIO_PLAYER_OK)
        return player->status;
    if (target_tick < player->current_tick)
        return PORTABLE_AUDIO_PLAYER_TICK_REGRESSION;
    while (1) {
        PortableAudioSequenceEvent selected_event;
        uint32_t selected_tick = UINT32_MAX;
        uint32_t selected_index = UINT32_MAX;
        uint8_t selected_track = 0u;
        uint8_t track;
        int found = 0;
        for (track = 0u; track < player->sequence->track_count; ++track) {
            PortableAudioSequenceEvent event;
            uint32_t tick;
            PortableAudioPlayerStatus status;
            if (!player->tracks[track].active)
                continue;
            status = effective_tick(player, track, &event, &tick);
            if (status != PORTABLE_AUDIO_PLAYER_OK) {
                player->status = status;
                return status;
            }
            if (tick < selected_tick ||
                (tick == selected_tick && player->tracks[track].cursor <
                    selected_index)) {
                found = 1;
                selected_tick = tick;
                selected_index = player->tracks[track].cursor;
                selected_track = track;
                selected_event = event;
            }
        }
        if (!found || selected_tick > target_tick) {
            player->current_tick = target_tick;
            return PORTABLE_AUDIO_PLAYER_OK;
        }
        if (processed == event_budget) {
            player->current_tick = selected_tick;
            return PORTABLE_AUDIO_PLAYER_EVENT_BUDGET;
        }
        player->current_tick = selected_tick;
        player->status = apply_event(player, selected_track, &selected_event);
        if (player->status != PORTABLE_AUDIO_PLAYER_OK)
            return player->status;
        if (sink != NULL && !sink(user,selected_tick,&selected_event,
                                  &player->tracks[selected_track])) {
            player->status = PORTABLE_AUDIO_PLAYER_EVENT_SINK;
            return player->status;
        }
        ++processed;
        ++player->events_processed;
    }
}

static uint32_t hash_byte(uint32_t hash, uint8_t value)
{
    return (hash ^ value) * UINT32_C(16777619);
}

static uint32_t hash_u32(uint32_t hash, uint32_t value)
{
    unsigned shift;
    for (shift = 0u; shift < 32u; shift += 8u)
        hash = hash_byte(hash, (uint8_t)(value >> shift));
    return hash;
}

uint32_t portable_audio_sequence_player_checksum(
    const PortableAudioSequencePlayer *player)
{
    uint32_t hash = UINT32_C(2166136261);
    uint8_t track;
    if (player == NULL || player->sequence == NULL)
        return 0u;
    hash = hash_u32(hash, player->current_tick);
    hash = hash_u32(hash, player->events_processed);
    hash = hash_u32(hash, player->tempo);
    for (track = 0u; track < player->sequence->track_count; ++track) {
        const PortableAudioTrackState *state = &player->tracks[track];
        hash = hash_u32(hash, state->cursor);
        hash = hash_u32(hash, state->tick_bias);
        hash = hash_u32(hash, state->note_count);
        hash = hash_u32(hash, state->voice);
        hash = hash_byte(hash, state->volume);
        hash = hash_byte(hash, state->pan);
        hash = hash_byte(hash, state->last_key);
        hash = hash_byte(hash, state->last_velocity);
        hash = hash_byte(hash, state->active);
    }
    for (unsigned index = 0u; index < 256u; ++index)
        hash = hash_byte(hash, (player->memory_area!=NULL?
            player->memory_area:player->memory)[index]);
    return hash;
}

const char *portable_audio_player_status_name(PortableAudioPlayerStatus status)
{
    static const char *const names[] = {
        "ok", "invalid argument", "tick regression", "tick overflow",
        "event budget", "bad sequence", "event sink"
    };
    return (unsigned)status < sizeof names / sizeof names[0] ? names[status] :
        "unknown status";
}
