#include "replay_stream.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int memory_read(void *context, uint8_t *byte)
{
    PortableReplayMemoryInput *input = context;
    if (input == NULL || input->position > input->size ||
        (input->bytes == NULL && input->size != 0u)) return -1;
    if (input->position == input->size) return 0;
    *byte = input->bytes[input->position++];
    return 1;
}

static size_t memory_write(void *context, const char *bytes, size_t size)
{
    PortableReplayMemoryOutput *output = context;
    if (output == NULL || output->size > output->capacity ||
        (output->bytes == NULL && output->capacity != 0u)) return 0;
    size_t count = output->capacity - output->size;
    if (count > size) count = size;
    if (count != 0u) memcpy(output->bytes + output->size, bytes, count);
    output->size += count;
    return count;
}

PortableReplayReader portable_replay_memory_reader(PortableReplayMemoryInput *input)
{
    return (PortableReplayReader){input, memory_read, 0};
}

PortableReplayWriter portable_replay_memory_writer(PortableReplayMemoryOutput *output)
{
    return (PortableReplayWriter){output, memory_write, 0};
}

char *portable_replay_read_line(char *line, size_t capacity, PortableReplayReader *reader)
{
    if (reader == NULL || reader->read == NULL || reader->failed ||
        line == NULL || capacity < 2u) return NULL;
    size_t count = 0u;
    while (count + 1u < capacity) {
        uint8_t byte;
        const int status = reader->read(reader->context, &byte);
        if (status < 0) {
            reader->failed = 1;
            return NULL;
        }
        if (status == 0) break;
        line[count++] = (char)byte;
        if (byte == '\n') break;
    }
    line[count] = '\0';
    return count != 0u ? line : NULL;
}

static int write_bytes(PortableReplayWriter *writer, const char *bytes, size_t size)
{
    if (writer == NULL || writer->write == NULL || writer->failed) return 0;
    if (writer->write(writer->context, bytes, size) != size) {
        writer->failed = 1;
        return 0;
    }
    return 1;
}

int portable_replay_puts(const char *text, PortableReplayWriter *writer)
{
    return write_bytes(writer, text, strlen(text)) ? 0 : -1;
}

int portable_replay_putc(char byte, PortableReplayWriter *writer)
{
    return write_bytes(writer, &byte, 1u) ? (unsigned char)byte : -1;
}

int portable_replay_printf(PortableReplayWriter *writer, const char *format, ...)
{
    /* Codec writes are bounded fragments, not complete TT state lines. */
    char text[512];
    va_list args;
    va_start(args, format);
    const int size = vsnprintf(text, sizeof text, format, args);
    va_end(args);
    if (size < 0 || (size_t)size >= sizeof text) {
        if (writer != NULL) writer->failed = 1;
        return -1;
    }
    return write_bytes(writer, text, (size_t)size) ? size : -1;
}
