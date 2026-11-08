#include "../platform/frontend_replay_file.h"
#include "frontend_input_replay.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "frontend input replay failed line %d: %s\n", \
                __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static const PortableFrontendDriverSlot drivers[PORTABLE_FRONTEND_DRIVER_SLOTS] = {
    {1u,1u,0u,0u,0u,0u,0u}, {2u,0u,0u,0u,0u,0u,0u}, {3u,0u,0u,0u,0u,0u,0u}, {4u,0u,0u,0u,0u,0u,0u}, {0u,1u,0u,0u,0u,0u,0u},
    {5u,0u,0u,0u,0u,0u,0u}, {6u,0u,0u,0u,0u,0u,0u}, {7u,0u,0u,0u,0u,0u,0u}, {8u,1u,0u,0u,0u,0u,0u},
};
static const PortableFrontendCourseSlot courses[] = {
    {24u, 0u, 0u, 1u}, {25u, 0u, 1u, 1u},
};
static const PortableFrontendConfig config = {
    drivers, PORTABLE_FRONTEND_DRIVER_SLOTS, courses,
    sizeof courses / sizeof courses[0],
};

static FILE *text_file(const char *text)
{
    FILE *file = tmpfile();
    if (file == NULL)
        return NULL;
    if (fwrite(text, 1u, strlen(text), file) != strlen(text) ||
        fflush(file) != 0 || fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    return file;
}

static int replay_text(const char *text,
                       PortableFrontendInputReplayStatus expected)
{
    PortableFrontendFlow flow;
    FILE *file = text_file(text);
    CHECK(file != NULL);
    CHECK(portable_frontend_flow_init(&flow, &config));
    CHECK(portable_frontend_input_replay_read_file(file, &flow, NULL, NULL) ==
          expected);
    fclose(file);
    return 0;
}

static int test_round_trip(int legacy)
{
    static const uint16_t buttons[] = {
        0u, PORTABLE_FRONTEND_BUTTON_A, 0u,
        PORTABLE_FRONTEND_BUTTON_RIGHT, 0u,
    };
    PortableFrontendFlow written, replayed;
    uint32_t tick_count = 0u;
    int cancelled = 0;
    FILE *file = tmpfile();
    CHECK(file != NULL);
    CHECK(portable_frontend_flow_init(&written, &config));
    if(legacy) CHECK(fputs("MKSC-FRONTEND-INPUT 1\n",file)>=0);
    else CHECK(portable_frontend_input_replay_write_header_file(file) ==
               PORTABLE_FRONTEND_INPUT_REPLAY_OK);
    for (uint32_t index = 0u;
         index < (uint32_t)(sizeof buttons / sizeof buttons[0]); ++index) {
        CHECK(portable_frontend_flow_tick(&written, buttons[index]));
        CHECK(portable_frontend_input_replay_write_tick_file(file, index,
              buttons[index], &written) == PORTABLE_FRONTEND_INPUT_REPLAY_OK);
    }
    CHECK(fflush(file) == 0 && fseek(file, 0L, SEEK_SET) == 0);
    CHECK(portable_frontend_flow_init(&replayed, &config));
    CHECK(portable_frontend_input_replay_read_file(file, &replayed, &tick_count,
          &cancelled) == PORTABLE_FRONTEND_INPUT_REPLAY_OK);
    CHECK(tick_count == sizeof buttons / sizeof buttons[0]);
    CHECK(!cancelled);
    CHECK(portable_frontend_flow_checksum(&written) ==
          portable_frontend_flow_checksum(&replayed));
    fclose(file);
    return 0;
}

static int test_cancel_round_trip(PortableFrontendPage destination)
{
    static const uint16_t after_cancel[] = {
        0u, PORTABLE_FRONTEND_BUTTON_RIGHT, 0u,
    };
    PortableFrontendFlow written, replayed;
    uint32_t tick_count=99u;
    int cancelled=0;
    FILE *file=tmpfile();
    CHECK(file!=NULL);
    CHECK(portable_frontend_flow_init(&written,&config));
    written.page=PORTABLE_FRONTEND_PAGE_RACE;
    replayed=written;
    CHECK(portable_frontend_input_replay_write_header_file(file)==
          PORTABLE_FRONTEND_INPUT_REPLAY_OK);
    CHECK(portable_frontend_flow_cancel_race_to(&written,destination));
    CHECK(portable_frontend_input_replay_write_cancel_file(file,&written)==
          PORTABLE_FRONTEND_INPUT_REPLAY_OK);
    for (uint32_t index = 0u;
         index < (uint32_t)(sizeof after_cancel / sizeof after_cancel[0]);
         ++index) {
        CHECK(portable_frontend_flow_tick(&written, after_cancel[index]));
        CHECK(portable_frontend_input_replay_write_tick_file(file, index,
              after_cancel[index], &written) ==
              PORTABLE_FRONTEND_INPUT_REPLAY_OK);
    }
    CHECK(fflush(file)==0&&fseek(file,0L,SEEK_SET)==0);
    CHECK(portable_frontend_input_replay_read_file(file,&replayed,&tick_count,
          &cancelled)==PORTABLE_FRONTEND_INPUT_REPLAY_OK);
    CHECK(tick_count==sizeof after_cancel / sizeof after_cancel[0]&&cancelled);
    CHECK(portable_frontend_flow_checksum(&written)==
          portable_frontend_flow_checksum(&replayed));
    fclose(file);
    return 0;
}

static int test_back_to_title_round_trip(void)
{
    PortableFrontendFlow written, replayed;
    uint32_t ticks=0u;
    FILE *file=tmpfile();
    CHECK(file!=NULL);
    CHECK(portable_frontend_flow_init(&written,&config));
    written.page=PORTABLE_FRONTEND_PAGE_MODE;
    replayed=written;
    CHECK(portable_frontend_input_replay_write_header_file(file)==PORTABLE_FRONTEND_INPUT_REPLAY_OK);
    for(uint32_t index=0u;index<PORTABLE_FRONTEND_TITLE_MODE_TICKS+2u;++index) {
        uint16_t buttons=index==0u ? PORTABLE_FRONTEND_BUTTON_B : 0u;
        CHECK(portable_frontend_flow_tick(&written,buttons));
        if(written.page==PORTABLE_FRONTEND_PAGE_TITLE_INTRO &&
           written.transition_ticks==0u && written.page_ticks==0u)
            CHECK(portable_frontend_flow_settle_title(&written));
        CHECK(portable_frontend_input_replay_write_tick_file(file,index,buttons,&written)==PORTABLE_FRONTEND_INPUT_REPLAY_OK);
    }
    CHECK(written.page==PORTABLE_FRONTEND_PAGE_TITLE_MENU);
    CHECK(fflush(file)==0 && fseek(file,0L,SEEK_SET)==0);
    CHECK(portable_frontend_input_replay_read_file(file,&replayed,&ticks,NULL)==PORTABLE_FRONTEND_INPUT_REPLAY_OK);
    CHECK(ticks==PORTABLE_FRONTEND_TITLE_MODE_TICKS+2u);
    CHECK(portable_frontend_flow_checksum(&written)==portable_frontend_flow_checksum(&replayed));
    fclose(file);
    return 0;
}

static int test_ghost_choice_round_trip(void)
{
    PortableFrontendFlow written,replayed;
    CHECK(portable_frontend_flow_init(&written,&config));
    written.page=PORTABLE_FRONTEND_PAGE_COURSE;written.course_phase=2u;
    written.mode_cursor=1u;written.selected_course=24u;
    replayed=written;
    FILE *file=tmpfile();CHECK(file!=NULL);
    CHECK(portable_frontend_input_replay_write_header_file(file)==PORTABLE_FRONTEND_INPUT_REPLAY_OK);
    CHECK(portable_frontend_flow_set_ghost_choices(&written,7u));
    CHECK(portable_frontend_input_replay_write_ghost_choices_file(file,7u)==PORTABLE_FRONTEND_INPUT_REPLAY_OK);
    const uint16_t buttons[]={PORTABLE_FRONTEND_BUTTON_DOWN,0u,PORTABLE_FRONTEND_BUTTON_DOWN,0u,PORTABLE_FRONTEND_BUTTON_A};
    for(unsigned i=0;i<sizeof buttons/sizeof buttons[0];++i){
        CHECK(portable_frontend_flow_tick(&written,buttons[i]));
        CHECK(portable_frontend_input_replay_write_tick_file(file,i,buttons[i],&written)==PORTABLE_FRONTEND_INPUT_REPLAY_OK);
    }
    CHECK(written.ghost_choice==2u&&fflush(file)==0&&fseek(file,0,SEEK_SET)==0);
    CHECK(portable_frontend_input_replay_read_file(file,&replayed,NULL,NULL)==PORTABLE_FRONTEND_INPUT_REPLAY_OK);
    CHECK(portable_frontend_flow_checksum(&written)==portable_frontend_flow_checksum(&replayed));
    CHECK(fclose(file)==0);
    CHECK(replay_text("MKSC-FRONTEND-INPUT 1\nG 8\n",PORTABLE_FRONTEND_INPUT_REPLAY_BAD_RECORD)==0);
    CHECK(replay_text("MKSC-FRONTEND-INPUT 1\nG 1\n",PORTABLE_FRONTEND_INPUT_REPLAY_BAD_RECORD)==0);
    return 0;
}

static int test_long_consumed_recording(void)
{
    PortableFrontendFlow written,replayed;
    CHECK(portable_frontend_flow_init(&written,&config));
    written.page=PORTABLE_FRONTEND_PAGE_CHARACTER;
    written.character_cursor=0u;
    replayed=written;
    FILE *file=tmpfile();CHECK(file!=NULL);
    CHECK(portable_frontend_input_replay_write_header_file(file)==PORTABLE_FRONTEND_INPUT_REPLAY_OK);
    for(uint32_t i=0u;i<2400u;++i){
        const uint16_t buttons=(i&1u)?0u:PORTABLE_FRONTEND_BUTTON_RIGHT;
        CHECK(portable_frontend_flow_tick(&written,buttons));
        CHECK(portable_frontend_input_replay_write_tick_file(file,i,buttons,&written)==PORTABLE_FRONTEND_INPUT_REPLAY_OK);
        if(written.event_count!=0u)
            CHECK(portable_frontend_input_replay_write_consume_file(file,&written)==PORTABLE_FRONTEND_INPUT_REPLAY_OK);
    }
    CHECK(!written.event_overflow&&written.event_count==0u);
    CHECK(fflush(file)==0&&fseek(file,0,SEEK_SET)==0);
    uint32_t ticks=0u;
    CHECK(portable_frontend_input_replay_read_file(file,&replayed,&ticks,NULL)==PORTABLE_FRONTEND_INPUT_REPLAY_OK);
    CHECK(ticks==2400u&&replayed.event_count==0u);
    CHECK(portable_frontend_flow_checksum(&written)==portable_frontend_flow_checksum(&replayed));
    CHECK(written.character_selection_tick==replayed.character_selection_tick);
    CHECK(fclose(file)==0);
    CHECK(replay_text("MKSC-FRONTEND-INPUT 1\nD 00000000\n",PORTABLE_FRONTEND_INPUT_REPLAY_BAD_RECORD)==0);
    CHECK(replay_text("MKSC-FRONTEND-INPUT 2\nD 00000000\n",PORTABLE_FRONTEND_INPUT_REPLAY_BAD_CHECKSUM)==0);
    return 0;
}

static int test_rejections(void)
{
    CHECK(replay_text("wrong\n", PORTABLE_FRONTEND_INPUT_REPLAY_BAD_HEADER) == 0);
    CHECK(replay_text("MKSC-FRONTEND-INPUT 1\nT 1 0000 00000000\n",
          PORTABLE_FRONTEND_INPUT_REPLAY_BAD_SEQUENCE) == 0);
    CHECK(replay_text("MKSC-FRONTEND-INPUT 1\nT 0 0400 00000000\n",
          PORTABLE_FRONTEND_INPUT_REPLAY_BAD_BUTTONS) == 0);
    CHECK(replay_text("MKSC-FRONTEND-INPUT 1\nT 0 0000 00000000\n",
          PORTABLE_FRONTEND_INPUT_REPLAY_BAD_CHECKSUM) == 0);
    CHECK(replay_text("MKSC-FRONTEND-INPUT 1\nC 00000000\n",
          PORTABLE_FRONTEND_INPUT_REPLAY_BAD_CANCEL) == 0);
    CHECK(replay_text("MKSC-FRONTEND-INPUT 1\nC 00000000 999\n",
          PORTABLE_FRONTEND_INPUT_REPLAY_BAD_CANCEL) == 0);
    CHECK(replay_text("MKSC-FRONTEND-INPUT 1\nC 00000000 junk\n",
          PORTABLE_FRONTEND_INPUT_REPLAY_BAD_RECORD) == 0);
    CHECK(replay_text("MKSC-FRONTEND-INPUT 1\nX 0\n",
          PORTABLE_FRONTEND_INPUT_REPLAY_BAD_RECORD) == 0);
    return 0;
}

static int test_bounded_numbers(void)
{
    static const char *const records[] = {
        "T 4294967296 0000 00000000\n",
        "T 0 100000000 00000000\n",
        "T 0 0000 100000000\n",
        "T 18446744073709551616 0000 00000000\n",
        "D 100000000\n", "G 4294967296\n", "C 00000000 4294967296\n",
    };
    for (size_t i = 0u; i < sizeof records / sizeof records[0]; ++i) {
        char text[256];
        CHECK(snprintf(text, sizeof text, "MKSC-FRONTEND-INPUT 2\n%s", records[i]) > 0);
        PortableReplayMemoryInput input = {(const uint8_t *)text, strlen(text), 0u};
        PortableReplayReader reader = portable_replay_memory_reader(&input);
        PortableFrontendFlow flow;
        CHECK(portable_frontend_flow_init(&flow, &config));
        const uint32_t before = portable_frontend_flow_checksum(&flow);
        CHECK(portable_frontend_input_replay_read(&reader, &flow, NULL, NULL) ==
              PORTABLE_FRONTEND_INPUT_REPLAY_BAD_RECORD);
        CHECK(portable_frontend_flow_checksum(&flow) == before);
    }
    CHECK(replay_text("MKSC-FRONTEND-INPUT 2\nT 4294967295 0000 00000000\n",
          PORTABLE_FRONTEND_INPUT_REPLAY_BAD_SEQUENCE) == 0);
    CHECK(replay_text("MKSC-FRONTEND-INPUT 2\nT 0 0000 ffffffff\n",
          PORTABLE_FRONTEND_INPUT_REPLAY_BAD_CHECKSUM) == 0);
    /* Retain scanf's valid optional plus, hex prefix and ASCII whitespace. */
    PortableFrontendFlow expected, played;
    CHECK(portable_frontend_flow_init(&expected, &config));
    CHECK(portable_frontend_flow_init(&played, &config));
    CHECK(portable_frontend_flow_tick(&expected, 0u));
    char text[128];
    CHECK(snprintf(text, sizeof text, "MKSC-FRONTEND-INPUT 2\nT +0 +0x0 +0x%08x \t\r\n",
                   portable_frontend_flow_checksum(&expected)) > 0);
    PortableReplayMemoryInput input = {(const uint8_t *)text, strlen(text), 0u};
    PortableReplayReader reader = portable_replay_memory_reader(&input);
    uint32_t ticks = 0u;
    CHECK(portable_frontend_input_replay_read(&reader, &played, &ticks, NULL) ==
          PORTABLE_FRONTEND_INPUT_REPLAY_OK);
    CHECK(ticks == 1u && portable_frontend_flow_checksum(&played) ==
                         portable_frontend_flow_checksum(&expected));
    return 0;
}

static int failed_read(void *context, uint8_t *byte)
{
    (void)context;
    (void)byte;
    return -1;
}

static int test_memory_transport(void)
{
    static const uint16_t buttons[] = {0u, PORTABLE_FRONTEND_BUTTON_A, 0u,
                                       PORTABLE_FRONTEND_BUTTON_RIGHT, 0u};
    uint8_t bytes[4096], file_bytes[4096];
    PortableReplayMemoryOutput output = {bytes, sizeof bytes, 0u};
    PortableReplayWriter writer = portable_replay_memory_writer(&output);
    PortableFrontendFlow written, played;
    FILE *file = tmpfile();
    CHECK(file != NULL);
    CHECK(portable_frontend_flow_init(&written, &config));
    CHECK(portable_frontend_input_replay_write_header(&writer) == PORTABLE_FRONTEND_INPUT_REPLAY_OK);
    CHECK(portable_frontend_input_replay_write_header_file(file) == PORTABLE_FRONTEND_INPUT_REPLAY_OK);
    CHECK(output.size == strlen("MKSC-FRONTEND-INPUT 2\n"));
    CHECK(memcmp(bytes, "MKSC-FRONTEND-INPUT 2\n", output.size) == 0);
    for (uint32_t tick = 0u; tick < sizeof buttons / sizeof buttons[0]; ++tick) {
        CHECK(portable_frontend_flow_tick(&written, buttons[tick]));
        const size_t before = output.size;
        char expected[128];
        const int count = snprintf(expected, sizeof expected, "T %u %04x %08x\n",
            tick, buttons[tick], portable_frontend_flow_checksum(&written));
        CHECK(count > 0);
        CHECK(portable_frontend_input_replay_write_tick(&writer, tick, buttons[tick], &written) ==
              PORTABLE_FRONTEND_INPUT_REPLAY_OK);
        CHECK(output.size - before == (size_t)count);
        CHECK(memcmp(bytes + before, expected, (size_t)count) == 0);
        CHECK(portable_frontend_input_replay_write_tick_file(file, tick, buttons[tick], &written) ==
              PORTABLE_FRONTEND_INPUT_REPLAY_OK);
    }
    CHECK(fflush(file) == 0);
    rewind(file);
    CHECK(fread(file_bytes, 1u, sizeof file_bytes, file) == output.size);
    CHECK(memcmp(file_bytes, bytes, output.size) == 0);
    CHECK(fclose(file) == 0);
    PortableReplayMemoryInput input = {bytes, output.size, 0u};
    PortableReplayReader reader = portable_replay_memory_reader(&input);
    uint32_t ticks = 99u;
    int cancelled = 1;
    CHECK(portable_frontend_flow_init(&played, &config));
    CHECK(portable_frontend_input_replay_read(&reader, &played, &ticks, &cancelled) ==
          PORTABLE_FRONTEND_INPUT_REPLAY_OK);
    CHECK(ticks == sizeof buttons / sizeof buttons[0] && !cancelled);
    CHECK(portable_frontend_flow_checksum(&played) == portable_frontend_flow_checksum(&written));

    /* A short write must not consume the event batch it failed to record. */
    output.size = 0u; output.capacity = 1u;
    writer = portable_replay_memory_writer(&output);
    const size_t event_count = written.event_count;
    CHECK(event_count != 0u);
    CHECK(portable_frontend_input_replay_write_consume(&writer, &written) ==
          PORTABLE_FRONTEND_INPUT_REPLAY_IO_ERROR);
    CHECK(writer.failed && output.size == 1u && written.event_count == event_count);
    CHECK(portable_frontend_input_replay_write_header(&writer) == PORTABLE_FRONTEND_INPUT_REPLAY_IO_ERROR);
    CHECK(output.size == 1u);
    reader = (PortableReplayReader){NULL, failed_read, 0};
    CHECK(portable_frontend_input_replay_read(&reader, &played, NULL, NULL) ==
          PORTABLE_FRONTEND_INPUT_REPLAY_IO_ERROR);
    CHECK(reader.failed);

    /* Like fgets, bounded reads leave the rest of an overlong line unread. */
    input = (PortableReplayMemoryInput){(const uint8_t *)"abcd\ne", 6u, 0u};
    reader = portable_replay_memory_reader(&input);
    char line[4];
    CHECK(portable_replay_read_line(line, sizeof line, &reader) && strcmp(line, "abc") == 0);
    CHECK(portable_replay_read_line(line, sizeof line, &reader) && strcmp(line, "d\n") == 0);
    CHECK(portable_replay_read_line(line, sizeof line, &reader) && strcmp(line, "e") == 0);
    CHECK(portable_replay_read_line(line, sizeof line, &reader) == NULL && !reader.failed);
    return 0;
}

int main(void)
{
    CHECK(test_bounded_numbers() == 0);
    CHECK(test_memory_transport() == 0);
    CHECK(test_round_trip(0) == 0);
    CHECK(test_round_trip(1) == 0);
    CHECK(test_long_consumed_recording()==0);
    CHECK(test_ghost_choice_round_trip()==0);
    CHECK(test_cancel_round_trip(PORTABLE_FRONTEND_PAGE_COURSE) == 0);
    CHECK(test_cancel_round_trip(PORTABLE_FRONTEND_PAGE_CHARACTER) == 0);
    CHECK(test_cancel_round_trip(PORTABLE_FRONTEND_PAGE_MODE) == 0);
    CHECK(test_back_to_title_round_trip() == 0);
    CHECK(test_rejections() == 0);
    puts("portable frontend input replay: PASS");
    return 0;
}
