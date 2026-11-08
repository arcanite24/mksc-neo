#ifndef PORTABLE_REPLAY_STREAM_H
#define PORTABLE_REPLAY_STREAM_H

#include <stddef.h>
#include <stdint.h>

/* One byte, end of input, or an I/O failure: 1, 0, -1 respectively. */
typedef int (*PortableReplayReadByte)(void *context, uint8_t *byte);
typedef size_t (*PortableReplayWriteBytes)(void *context, const char *bytes, size_t size);

typedef struct {
    void *context;
    PortableReplayReadByte read;
    int failed;
} PortableReplayReader;

typedef struct {
    void *context;
    PortableReplayWriteBytes write;
    int failed;
} PortableReplayWriter;

typedef struct {
    const uint8_t *bytes;
    size_t size, position;
} PortableReplayMemoryInput;

typedef struct {
    uint8_t *bytes;
    size_t capacity, size;
} PortableReplayMemoryOutput;

PortableReplayReader portable_replay_memory_reader(PortableReplayMemoryInput *input);
PortableReplayWriter portable_replay_memory_writer(PortableReplayMemoryOutput *output);
char *portable_replay_read_line(char *line, size_t capacity, PortableReplayReader *reader);
int portable_replay_puts(const char *text, PortableReplayWriter *writer);
int portable_replay_putc(char byte, PortableReplayWriter *writer);
/* Bounded replay text needs only integer/string conversions, width and zero padding. */
int portable_replay_printf(PortableReplayWriter *writer, const char *format, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

#endif
