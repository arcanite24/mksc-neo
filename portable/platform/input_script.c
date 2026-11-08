#include "input_script.h"

#include "driver_input.h"

static int token_buttons(char token, uint16_t *out_buttons)
{
    if (out_buttons == NULL)
        return PORTABLE_INPUT_SCRIPT_INVALID_ARGUMENT;

    switch (token)
    {
    case '.':
        *out_buttons = PORTABLE_INPUT_NONE;
        return PORTABLE_INPUT_SCRIPT_OK;
    case 'A':
        *out_buttons = PORTABLE_INPUT_ACCELERATE;
        return PORTABLE_INPUT_SCRIPT_OK;
    case 'B':
        *out_buttons = PORTABLE_INPUT_BRAKE;
        return PORTABLE_INPUT_SCRIPT_OK;
    case 'l':
        *out_buttons = PORTABLE_INPUT_LEFT;
        return PORTABLE_INPUT_SCRIPT_OK;
    case 'r':
        *out_buttons = PORTABLE_INPUT_RIGHT;
        return PORTABLE_INPUT_SCRIPT_OK;
    case 'L':
        *out_buttons = PORTABLE_INPUT_ACCELERATE | PORTABLE_INPUT_LEFT;
        return PORTABLE_INPUT_SCRIPT_OK;
    case 'R':
        *out_buttons = PORTABLE_INPUT_ACCELERATE | PORTABLE_INPUT_RIGHT;
        return PORTABLE_INPUT_SCRIPT_OK;
    default:
        return PORTABLE_INPUT_SCRIPT_INVALID_TOKEN;
    }
}

int portable_input_script_parse(const char *script,
                                PortableInputFrame *frames,
                                size_t capacity,
                                size_t *out_count)
{
    size_t count = 0;

    if (script == NULL || out_count == NULL ||
        (capacity != 0u && frames == NULL))
        return PORTABLE_INPUT_SCRIPT_INVALID_ARGUMENT;

    for (const char *cursor = script; *cursor != '\0'; ++cursor)
    {
        uint16_t buttons;
        const int result = token_buttons(*cursor, &buttons);

        if (result != PORTABLE_INPUT_SCRIPT_OK)
            return result;
        if (count >= capacity)
            return PORTABLE_INPUT_SCRIPT_CAPACITY;

        frames[count].buttons = buttons;
        frames[count].source_control_word =
            portable_driver_control_word_from_semantic_buttons(buttons);
        ++count;
    }

    *out_count = count;
    return PORTABLE_INPUT_SCRIPT_OK;
}


static int mesh_segment_number(const char **cursor, uint32_t maximum, uint32_t *out)
{
    const char *p = *cursor;
    uint32_t value = 0;
    if (*p < '0' || *p > '9')
        return 0;
    do
    {
        const uint32_t digit = (uint32_t)(*p++ - '0');
        if (digit > maximum || value > (maximum - digit) / 10u)
            return 0;
        value = value * 10u + digit;
    } while (*p >= '0' && *p <= '9');
    *cursor = p;
    *out = value;
    return 1;
}

static int mesh_segments_valid(const PortableMeshInputSegments *segments)
{
    if (!segments || segments->count > PORTABLE_MESH_INPUT_SEGMENT_CAPACITY)
        return 0;
    uint32_t previous_end = 0, total = 0;
    for (size_t i = 0; i < segments->count; ++i)
    {
        const PortableMeshInputSegment *s = &segments->segments[i];
        if (!s->count || s->count > PORTABLE_MESH_INPUT_SEGMENT_TICKS - total ||
            s->first_tick > UINT32_MAX - s->count ||
            (i && s->first_tick < previous_end) || s->input.steering < -32767 ||
            (s->input.buttons & ~(PORTABLE_MESH_ACCELERATE | PORTABLE_MESH_BRAKE | PORTABLE_MESH_HOP)))
            return 0;
        total += s->count;
        previous_end = s->first_tick + s->count;
    }
    return 1;
}

int portable_mesh_input_segments_parse(const char *text, PortableMeshInputSegments *out)
{
    if (!text || !out)
        return PORTABLE_INPUT_SCRIPT_INVALID_ARGUMENT;
    /* Bound even strings of leading zeros before parsing numeric fields. */
    size_t length = 0;
    while (length <= 2048u && text[length])
        ++length;
    if (!length || length > 2048u)
        return PORTABLE_INPUT_SCRIPT_INVALID_TOKEN;
    PortableMeshInputSegments next = {0};
    const char *p = text;
    while (*p)
    {
        if (next.count == PORTABLE_MESH_INPUT_SEGMENT_CAPACITY)
            return PORTABLE_INPUT_SCRIPT_CAPACITY;
        PortableMeshInputSegment *segment = &next.segments[next.count];
        uint32_t steering, buttons;
        if (!mesh_segment_number(&p, UINT32_MAX, &segment->first_tick) || *p++ != ':' ||
            !mesh_segment_number(&p, PORTABLE_MESH_INPUT_SEGMENT_TICKS, &segment->count) || *p++ != ':')
            return PORTABLE_INPUT_SCRIPT_INVALID_TOKEN;
        const int negative = *p == '-';
        if (negative)
            ++p;
        if (!mesh_segment_number(&p, 32767u, &steering) || *p++ != ':' ||
            !mesh_segment_number(&p, 7u, &buttons))
            return PORTABLE_INPUT_SCRIPT_INVALID_TOKEN;
        segment->input.steering = (int16_t)(negative ? -(int32_t)steering : (int32_t)steering);
        segment->input.buttons = (uint16_t)buttons;
        ++next.count;
        if (*p && *p != ';')
            return PORTABLE_INPUT_SCRIPT_INVALID_TOKEN;
        if (*p == ';' && !*++p)
            return PORTABLE_INPUT_SCRIPT_INVALID_TOKEN;
    }
    if (!mesh_segments_valid(&next))
        return PORTABLE_INPUT_SCRIPT_INVALID_TOKEN;
    *out = next;
    return PORTABLE_INPUT_SCRIPT_OK;
}

int portable_mesh_input_segments_sample(const PortableMeshInputSegments *segments,
                                         uint32_t tick, PortableMeshKartInput *out)
{
    if (!out || !mesh_segments_valid(segments))
        return -1;
    for (size_t i = 0; i < segments->count; ++i)
    {
        const PortableMeshInputSegment *s = &segments->segments[i];
        if (tick >= s->first_tick && tick - s->first_tick < s->count)
        {
            *out = s->input;
            return 1;
        }
    }
    return 0;
}

int portable_mesh_input_race_parse(const char *text, uint32_t *out)
{
    if (!out)
        return PORTABLE_INPUT_SCRIPT_INVALID_ARGUMENT;
    if (text && (text[0] < '0' || text[0] > '3' || text[1]))
        return PORTABLE_INPUT_SCRIPT_INVALID_TOKEN;
    *out = text ? (uint32_t)(text[0] - '0') : UINT32_MAX;
    return PORTABLE_INPUT_SCRIPT_OK;
}

int portable_mesh_input_segments_sample_race(const PortableMeshInputSegments *segments,
    uint32_t selected_race, uint32_t current_race, uint32_t tick, PortableMeshKartInput *out)
{
    if (!out || !mesh_segments_valid(segments) ||
        (selected_race != UINT32_MAX && selected_race >= 4u) ||
        (current_race != UINT32_MAX && current_race >= 4u) ||
        (selected_race != UINT32_MAX && current_race == UINT32_MAX))
        return -1;
    if (selected_race != UINT32_MAX && selected_race != current_race)
        return 0;
    return portable_mesh_input_segments_sample(segments, tick, out);
}
