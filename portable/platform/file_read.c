#include "file_read.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    void *stream;
    size_t (*read)(void *, void *, size_t, int *, int *);
    int (*close)(void *);
} FileReader;

static PortableFileReadStatus read_bounded(FileReader reader, size_t maximum,
    uint8_t **bytes, size_t *size)
{
    PortableFileReadStatus status = PORTABLE_FILE_READ_INVALID;
    uint8_t *buffer = NULL;
    size_t length = 0u;
    if (reader.stream == NULL) return PORTABLE_FILE_READ_OPEN;
    if (bytes == NULL || size == NULL || maximum == SIZE_MAX) goto close;
    const size_t limit = maximum + 1u;
    size_t capacity = limit < 65536u ? limit : 65536u;
    buffer = malloc(capacity);
    if (buffer == NULL) {status = PORTABLE_FILE_READ_MEMORY; goto close;}
    for (;;) {
        if (length == capacity) {
            const size_t next = capacity > limit / 2u ? limit : capacity * 2u;
            uint8_t *grown = realloc(buffer, next);
            if (grown == NULL) {status = PORTABLE_FILE_READ_MEMORY; break;}
            buffer = grown;
            capacity = next;
        }
        int eof = 0, error = 0;
        const size_t available = capacity - length;
        const size_t received = reader.read(reader.stream, buffer + length,
            available, &eof, &error);
        if (received > available || error) {status = PORTABLE_FILE_READ_IO; break;}
        length += received;
        if (length > maximum) {status = PORTABLE_FILE_READ_LIMIT; break;}
        if (received == 0u) {
            status = eof ? PORTABLE_FILE_READ_OK : PORTABLE_FILE_READ_IO;
            break;
        }
    }
close:
    if (!reader.close(reader.stream)) status = PORTABLE_FILE_READ_IO;
    if (status != PORTABLE_FILE_READ_OK) {free(buffer); return status;}
    uint8_t *smaller = realloc(buffer, length != 0u ? length : 1u);
    if (smaller != NULL) buffer = smaller;
    *bytes = buffer;
    *size = length;
    return PORTABLE_FILE_READ_OK;
}

#ifdef PORTABLE_ENABLE_SDL3
static size_t stream_read(void *stream, void *bytes, size_t size, int *eof, int *error)
{
    const size_t received = SDL_ReadIO(stream, bytes, size);
    const SDL_IOStatus status = SDL_GetIOStatus(stream);
    *eof = status == SDL_IO_STATUS_EOF;
    *error = status != SDL_IO_STATUS_READY && status != SDL_IO_STATUS_EOF;
    return received;
}
static int stream_close(void *stream) {return SDL_CloseIO(stream);}

PortableFileReadStatus portable_file_read_stream(SDL_IOStream *stream, size_t maximum,
    uint8_t **bytes, size_t *size)
{
    return read_bounded((FileReader){stream, stream_read, stream_close}, maximum, bytes, size);
}
#else
static size_t stream_read(void *stream, void *bytes, size_t size, int *eof, int *error)
{
    const size_t received = fread(bytes, 1u, size, stream);
    *eof = feof(stream);
    *error = ferror(stream);
    return received;
}
static int stream_close(void *stream) {return fclose(stream) == 0;}
#endif

PortableFileReadStatus portable_file_read_bounded(const char *path, size_t maximum,
    uint8_t **bytes, size_t *size)
{
    if (path == NULL || bytes == NULL || size == NULL || maximum == SIZE_MAX)
        return PORTABLE_FILE_READ_INVALID;
#ifdef PORTABLE_ENABLE_SDL3
    errno = 0;
    SDL_IOStream *stream = SDL_IOFromFile(path, "rb");
    if (stream == NULL && strncmp(path, "content://", 10u) != 0 && errno == ENOENT)
        return PORTABLE_FILE_READ_NOT_FOUND;
    return portable_file_read_stream(stream, maximum, bytes, size);
#else
    FILE *stream = fopen(path, "rb");
    if (stream == NULL && strncmp(path, "content://", 10u) != 0 && errno == ENOENT)
        return PORTABLE_FILE_READ_NOT_FOUND;
    return read_bounded((FileReader){stream, stream_read, stream_close}, maximum, bytes, size);
#endif
}
