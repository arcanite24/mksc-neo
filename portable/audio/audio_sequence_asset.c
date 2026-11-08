#include "audio_sequence_asset.h"

#include "audio_command_stream.h"

#include <string.h>

static uint16_t read_u16(const uint8_t *data)
{
    return (uint16_t)(data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t read_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
        ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static int zero_except_time_track(const PortableAudioSequenceEvent *event)
{
    return event->target == 0u && event->value == 0u && event->arg0 == 0u &&
        event->arg1 == 0u;
}

static PortableAudioSequenceStatus read_event(
    const PortableAudioSequenceView *view, uint32_t index,
    PortableAudioSequenceEvent *event)
{
    const uint8_t *source;
    if (index >= view->event_count)
        return PORTABLE_AUDIO_SEQUENCE_BAD_SIZE;
    source = view->data + view->events_offset +
        (size_t)index * PORTABLE_AUDIO_SEQUENCE_EVENT_SIZE;
    event->tick = read_u32(source);
    event->target = read_u32(source + 4u);
    event->value = read_u16(source + 8u);
    event->track = source[10];
    event->operation = source[11];
    event->arg0 = source[12];
    event->arg1 = source[13];
    if (read_u16(source + 14u) != 0u)
        return PORTABLE_AUDIO_SEQUENCE_BAD_PARAMETER;
    return PORTABLE_AUDIO_SEQUENCE_OK;
}

static PortableAudioSequenceStatus validate_event(
    const PortableAudioSequenceView *view, uint32_t index,
    const PortableAudioSequenceEvent *event)
{
    PortableAudioSequenceEvent target;
    if (event->track >= view->track_count)
        return PORTABLE_AUDIO_SEQUENCE_BAD_TRACK;
    switch ((PortableAudioSequenceOperation)event->operation) {
    case PORTABLE_AUDIO_EVENT_END:
    case PORTABLE_AUDIO_EVENT_LOOP_POINT:
        return zero_except_time_track(event) ? PORTABLE_AUDIO_SEQUENCE_OK :
            PORTABLE_AUDIO_SEQUENCE_BAD_PARAMETER;
    case PORTABLE_AUDIO_EVENT_TEMPO:
        if (event->target != 0u || event->value == 0u || event->value > 255u ||
            event->arg0 != 0u || event->arg1 != 0u)
            return PORTABLE_AUDIO_SEQUENCE_BAD_PARAMETER;
        return PORTABLE_AUDIO_SEQUENCE_OK;
    case PORTABLE_AUDIO_EVENT_PRIORITY:
    case PORTABLE_AUDIO_EVENT_KEY_SHIFT:
    case PORTABLE_AUDIO_EVENT_VOICE:
    case PORTABLE_AUDIO_EVENT_VOLUME:
    case PORTABLE_AUDIO_EVENT_PAN:
    case PORTABLE_AUDIO_EVENT_BEND:
    case PORTABLE_AUDIO_EVENT_BEND_RANGE:
    case PORTABLE_AUDIO_EVENT_LFO_SPEED:
    case PORTABLE_AUDIO_EVENT_LFO_DELAY:
    case PORTABLE_AUDIO_EVENT_MOD_DEPTH:
    case PORTABLE_AUDIO_EVENT_MOD_TYPE:
    case PORTABLE_AUDIO_EVENT_TUNE:
        if (event->target != 0u || event->value > 255u || event->arg0 != 0u ||
            event->arg1 != 0u)
            return PORTABLE_AUDIO_SEQUENCE_BAD_PARAMETER;
        return PORTABLE_AUDIO_SEQUENCE_OK;
    case PORTABLE_AUDIO_EVENT_NOTE:
        if (event->target != 0u || event->value == 0u || event->value > 255u ||
            event->arg0 > 127u || event->arg1 > 127u)
            return PORTABLE_AUDIO_SEQUENCE_BAD_PARAMETER;
        return PORTABLE_AUDIO_SEQUENCE_OK;
    case PORTABLE_AUDIO_EVENT_TIE:
        if (event->target != 0u || event->value != 0u || event->arg0 > 127u ||
            event->arg1 > 127u)
            return PORTABLE_AUDIO_SEQUENCE_BAD_PARAMETER;
        return PORTABLE_AUDIO_SEQUENCE_OK;
    case PORTABLE_AUDIO_EVENT_END_TIE:
        if (event->target != 0u || event->value != 0u || event->arg1 != 0u ||
            event->arg0 > 127u)
            return PORTABLE_AUDIO_SEQUENCE_BAD_PARAMETER;
        return PORTABLE_AUDIO_SEQUENCE_OK;
    case PORTABLE_AUDIO_EVENT_MEMORY_SET:
        if (event->target != 0u || event->value != 0u)
            return PORTABLE_AUDIO_SEQUENCE_BAD_PARAMETER;
        return PORTABLE_AUDIO_SEQUENCE_OK;
    case PORTABLE_AUDIO_EVENT_LOOP:
        if (event->target >= index || event->value != 0u || event->arg0 != 0u ||
            event->arg1 != 0u)
            return PORTABLE_AUDIO_SEQUENCE_BAD_TARGET;
        if (read_event(view, event->target, &target) !=
                PORTABLE_AUDIO_SEQUENCE_OK || target.track != event->track ||
                target.tick >= event->tick)
            return PORTABLE_AUDIO_SEQUENCE_BAD_TARGET;
        return PORTABLE_AUDIO_SEQUENCE_OK;
    default:
        return PORTABLE_AUDIO_SEQUENCE_BAD_OPERATION;
    }
}

PortableAudioSequenceStatus portable_audio_sequence_open(
    PortableAudioSequenceView *view, const uint8_t *data, size_t size)
{
    static const uint8_t magic[8] = {'M','K','S','C','A','U','D','1'};
    PortableAudioSequenceView candidate;
    uint32_t previous_tick = 0u;
    uint32_t terminated = 0u;
    uint32_t index;
    if (view == NULL || data == NULL)
        return PORTABLE_AUDIO_SEQUENCE_INVALID_ARGUMENT;
    if (size < PORTABLE_AUDIO_SEQUENCE_HEADER_SIZE)
        return PORTABLE_AUDIO_SEQUENCE_BAD_SIZE;
    if (memcmp(data, magic, sizeof magic) != 0)
        return PORTABLE_AUDIO_SEQUENCE_BAD_MAGIC;
    if (read_u16(data + 8u) != PORTABLE_AUDIO_SEQUENCE_HEADER_SIZE ||
        (read_u16(data + 10u) != 1u && read_u16(data + 10u) != 2u))
        return PORTABLE_AUDIO_SEQUENCE_BAD_VERSION;
    candidate.data = data;
    candidate.size = size;
    candidate.song_id = read_u16(data + 12u);
    candidate.track_count = data[14];
    candidate.priority = data[15];
    candidate.reverb = data[16];
    candidate.player_index = read_u16(data+10u)==2u?data[17]:PORTABLE_AUDIO_PLAYER_AUTO;
    candidate.bank_id = read_u16(data + 18u);
    candidate.event_count = read_u32(data + 20u);
    candidate.events_offset = read_u32(data + 24u);
    if ((read_u16(data+10u)==1u?data[17]!=0u:data[17]>=PORTABLE_AUDIO_PLAYER_COUNT) ||
        candidate.bank_id == 0u ||
        read_u32(data + 28u) != size || candidate.events_offset <
            PORTABLE_AUDIO_SEQUENCE_HEADER_SIZE ||
        candidate.events_offset > size || candidate.event_count >
            (size - candidate.events_offset) /
                PORTABLE_AUDIO_SEQUENCE_EVENT_SIZE ||
        candidate.events_offset + (size_t)candidate.event_count *
            PORTABLE_AUDIO_SEQUENCE_EVENT_SIZE != size)
        return PORTABLE_AUDIO_SEQUENCE_BAD_SIZE;
    if (candidate.song_id >= PORTABLE_AUDIO_SONG_COUNT)
        return PORTABLE_AUDIO_SEQUENCE_BAD_SONG;
    if (candidate.track_count == 0u ||
        candidate.track_count > PORTABLE_AUDIO_SEQUENCE_MAX_TRACKS)
        return PORTABLE_AUDIO_SEQUENCE_BAD_TRACK;
    for (index = 0u; index < candidate.event_count; ++index) {
        PortableAudioSequenceEvent event;
        PortableAudioSequenceStatus status = read_event(&candidate, index,
                                                         &event);
        if (status != PORTABLE_AUDIO_SEQUENCE_OK)
            return status;
        if (index != 0u && event.tick < previous_tick)
            return PORTABLE_AUDIO_SEQUENCE_BAD_ORDER;
        previous_tick = event.tick;
        status = validate_event(&candidate, index, &event);
        if (status != PORTABLE_AUDIO_SEQUENCE_OK)
            return status;
        if ((terminated & (UINT32_C(1) << event.track)) != 0u)
            return PORTABLE_AUDIO_SEQUENCE_BAD_ORDER;
        if (event.operation == PORTABLE_AUDIO_EVENT_END ||
            event.operation == PORTABLE_AUDIO_EVENT_LOOP)
            terminated |= UINT32_C(1) << event.track;
    }
    if (terminated != (UINT32_C(1) << candidate.track_count) - 1u)
        return PORTABLE_AUDIO_SEQUENCE_UNTERMINATED_TRACK;
    *view = candidate;
    return PORTABLE_AUDIO_SEQUENCE_OK;
}

PortableAudioSequenceStatus portable_audio_sequence_event(
    const PortableAudioSequenceView *view, uint32_t index,
    PortableAudioSequenceEvent *event)
{
    if (view == NULL || event == NULL || view->data == NULL)
        return PORTABLE_AUDIO_SEQUENCE_INVALID_ARGUMENT;
    return read_event(view, index, event);
}

const char *portable_audio_sequence_status_name(
    PortableAudioSequenceStatus status)
{
    static const char *const names[] = {
        "ok", "invalid argument", "bad magic", "bad version", "bad size",
        "bad song", "bad track", "bad order", "bad operation",
        "bad parameter", "bad target", "unterminated track"
    };
    return (unsigned)status < sizeof names / sizeof names[0] ? names[status] :
        "unknown status";
}
