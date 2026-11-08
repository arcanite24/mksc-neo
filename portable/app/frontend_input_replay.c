#include "frontend_input_replay.h"

#include <inttypes.h>
#include <ctype.h>
#include "../core/text_number.h"
#include <string.h>

#define FRONTEND_INPUT_HEADER "MKSC-FRONTEND-INPUT 2"
#define FRONTEND_INPUT_LINE_CAPACITY 128u
#define FRONTEND_INPUT_BUTTON_MASK UINT16_C(0x03ff)

static PortableFrontendInputReplayStatus write_result(PortableReplayWriter *writer, int result)
{
    if (result < 0 || writer->failed)
        return PORTABLE_FRONTEND_INPUT_REPLAY_IO_ERROR;
    return PORTABLE_FRONTEND_INPUT_REPLAY_OK;
}

PortableFrontendInputReplayStatus portable_frontend_input_replay_write_header(
    PortableReplayWriter *writer)
{
    if (writer == NULL)
        return PORTABLE_FRONTEND_INPUT_REPLAY_INVALID_ARGUMENT;
    return write_result(writer, portable_replay_printf(writer, "%s\n", FRONTEND_INPUT_HEADER));
}

PortableFrontendInputReplayStatus portable_frontend_input_replay_write_ghost_choices(
    PortableReplayWriter *writer,uint8_t mask)
{
    if(writer==NULL||mask>7u)return PORTABLE_FRONTEND_INPUT_REPLAY_INVALID_ARGUMENT;
    return write_result(writer,portable_replay_printf(writer,"G %u\n",(unsigned)mask));
}

PortableFrontendInputReplayStatus portable_frontend_input_replay_write_tick(
    PortableReplayWriter *writer, uint32_t index, uint16_t buttons,
    const PortableFrontendFlow *flow)
{
    if (writer == NULL || flow == NULL)
        return PORTABLE_FRONTEND_INPUT_REPLAY_INVALID_ARGUMENT;
    if ((buttons & (uint16_t)~FRONTEND_INPUT_BUTTON_MASK) != 0u)
        return PORTABLE_FRONTEND_INPUT_REPLAY_BAD_BUTTONS;
    return write_result(writer, portable_replay_printf(writer, "T %" PRIu32 " %04" PRIx16
        " %08" PRIx32 "\n", index, buttons,
        portable_frontend_flow_checksum(flow)));
}

PortableFrontendInputReplayStatus portable_frontend_input_replay_write_cancel(
    PortableReplayWriter *writer, const PortableFrontendFlow *flow)
{
    if (writer == NULL || flow == NULL)
        return PORTABLE_FRONTEND_INPUT_REPLAY_INVALID_ARGUMENT;
    return write_result(writer, portable_replay_printf(writer, "C %08" PRIx32 " %u\n",
        portable_frontend_flow_checksum(flow), (unsigned)flow->page));
}

PortableFrontendInputReplayStatus portable_frontend_input_replay_write_consume(
    PortableReplayWriter *writer, PortableFrontendFlow *flow)
{
    if (writer == NULL || flow == NULL)
        return PORTABLE_FRONTEND_INPUT_REPLAY_INVALID_ARGUMENT;
    const PortableFrontendInputReplayStatus status = write_result(writer,
        portable_replay_printf(writer, "D %08" PRIx32 "\n", portable_frontend_flow_checksum(flow)));
    if (status == PORTABLE_FRONTEND_INPUT_REPLAY_OK)
        portable_frontend_flow_consume_events(flow);
    return status;
}

static int read_u32(const char **cursor, const char *end, unsigned base, uint32_t *value)
{
    uint64_t parsed;
    if (!portable_text_u64(cursor, end, base, UINT32_MAX, &parsed)) return 0;
    *value = (uint32_t)parsed;
    return 1;
}

static int text_ended(const char *cursor, const char *end)
{
    while (cursor < end && isspace((unsigned char)*cursor)) ++cursor;
    return cursor == end;
}

PortableFrontendInputReplayStatus portable_frontend_input_replay_read(
    PortableReplayReader *reader, PortableFrontendFlow *flow, uint32_t *out_tick_count,
    int *out_cancelled)
{
    char line[FRONTEND_INPUT_LINE_CAPACITY];
    uint32_t expected_index = 0u;
    int cancelled = 0;
    if (reader == NULL || flow == NULL)
        return PORTABLE_FRONTEND_INPUT_REPLAY_INVALID_ARGUMENT;
    if (portable_replay_read_line(line, sizeof line, reader) == NULL)
        return reader->failed ? PORTABLE_FRONTEND_INPUT_REPLAY_IO_ERROR :
            PORTABLE_FRONTEND_INPUT_REPLAY_BAD_HEADER;
    line[strcspn(line, "\r\n")] = '\0';
    const int version2 = strcmp(line, FRONTEND_INPUT_HEADER) == 0;
    if (!version2 && strcmp(line, "MKSC-FRONTEND-INPUT 1") != 0)
        return PORTABLE_FRONTEND_INPUT_REPLAY_BAD_HEADER;
    while (portable_replay_read_line(line, sizeof line, reader) != NULL) {
        const char *cursor = line + 1;
        const char *end = line + strlen(line);
        uint32_t index, buttons, checksum;
        if (line[0] == 'T') {
            if (!read_u32(&cursor, end, 10u, &index) ||
                !read_u32(&cursor, end, 16u, &buttons) ||
                !read_u32(&cursor, end, 16u, &checksum) || !text_ended(cursor, end))
                return PORTABLE_FRONTEND_INPUT_REPLAY_BAD_RECORD;
            if (index != expected_index)
                return PORTABLE_FRONTEND_INPUT_REPLAY_BAD_SEQUENCE;
            if ((buttons & ~((uint32_t)FRONTEND_INPUT_BUTTON_MASK)) != 0u)
                return PORTABLE_FRONTEND_INPUT_REPLAY_BAD_BUTTONS;
            if (!portable_frontend_flow_tick(flow, (uint16_t)buttons))
                return PORTABLE_FRONTEND_INPUT_REPLAY_BAD_BUTTONS;
            /* The application skips the title introduction when Back completes. */
            if (flow->page == PORTABLE_FRONTEND_PAGE_TITLE_INTRO &&
                flow->transition_ticks == 0u && flow->page_ticks == 0u &&
                !portable_frontend_flow_settle_title(flow))
                return PORTABLE_FRONTEND_INPUT_REPLAY_BAD_RECORD;
            if (portable_frontend_flow_checksum(flow) != checksum)
                return PORTABLE_FRONTEND_INPUT_REPLAY_BAD_CHECKSUM;
            ++expected_index;
        } else if(line[0]=='D' && version2){
            if(!read_u32(&cursor,end,16u,&checksum)||!text_ended(cursor,end))
                return PORTABLE_FRONTEND_INPUT_REPLAY_BAD_RECORD;
            if(portable_frontend_flow_checksum(flow)!=checksum)
                return PORTABLE_FRONTEND_INPUT_REPLAY_BAD_CHECKSUM;
            portable_frontend_flow_consume_events(flow);
        } else if(line[0]=='G'){
            uint32_t mask;
            if(!read_u32(&cursor,end,10u,&mask)||!text_ended(cursor,end)||mask>7u||
               flow->page!=PORTABLE_FRONTEND_PAGE_COURSE||flow->course_phase!=2u||
               flow->mode_cursor!=1u||flow->transition_ticks!=0u||
               !portable_frontend_flow_set_ghost_choices(flow,(uint8_t)mask))
                return PORTABLE_FRONTEND_INPUT_REPLAY_BAD_RECORD;
        } else if (line[0] == 'C') {
            uint32_t destination = PORTABLE_FRONTEND_PAGE_COURSE;
            if (!read_u32(&cursor, end, 16u, &checksum))
                return PORTABLE_FRONTEND_INPUT_REPLAY_BAD_RECORD;
            /* Older recordings omit the destination entirely. */
            if (!text_ended(cursor, end) &&
                (!read_u32(&cursor, end, 10u, &destination) || !text_ended(cursor, end)))
                return PORTABLE_FRONTEND_INPUT_REPLAY_BAD_RECORD;
            if (!portable_frontend_flow_cancel_race_to(flow,
                    (PortableFrontendPage)destination))
                return PORTABLE_FRONTEND_INPUT_REPLAY_BAD_CANCEL;
            if (portable_frontend_flow_checksum(flow) != checksum)
                return PORTABLE_FRONTEND_INPUT_REPLAY_BAD_CHECKSUM;
            cancelled = 1;
        } else {
            return PORTABLE_FRONTEND_INPUT_REPLAY_BAD_RECORD;
        }
    }
    if (reader->failed)
        return PORTABLE_FRONTEND_INPUT_REPLAY_IO_ERROR;
    if (out_tick_count != NULL)
        *out_tick_count = expected_index;
    if (out_cancelled != NULL)
        *out_cancelled = cancelled;
    return PORTABLE_FRONTEND_INPUT_REPLAY_OK;
}

const char *portable_frontend_input_replay_status_name(
    PortableFrontendInputReplayStatus status)
{
    static const char *const names[] = {
        "ok", "invalid argument", "I/O error", "bad header", "bad record",
        "bad sequence", "bad buttons", "bad checksum", "bad cancel"
    };
    return (unsigned)status < sizeof names / sizeof names[0] ? names[status] :
        "unknown status";
}
