#ifndef PORTABLE_FILE_READ_H
#define PORTABLE_FILE_READ_H
#include <stddef.h>
#include <stdint.h>

typedef enum {
    PORTABLE_FILE_READ_OK = 0,
    PORTABLE_FILE_READ_INVALID,
    PORTABLE_FILE_READ_OPEN,
    PORTABLE_FILE_READ_MEMORY,
    PORTABLE_FILE_READ_IO,
    PORTABLE_FILE_READ_LIMIT,
    PORTABLE_FILE_READ_NOT_FOUND
} PortableFileReadStatus;

/* Reads native files or SDL-supported document URIs without requiring seek or
 * a known size. Success publishes malloc-owned bytes (also for an empty file).
 * Failure leaves both outputs untouched. Reads at most maximum+1 bytes. */
PortableFileReadStatus portable_file_read_bounded(const char *path, size_t maximum,
    uint8_t **bytes, size_t *size);

#ifdef PORTABLE_ENABLE_SDL3
#include <SDL3/SDL_iostream.h>
/* Takes ownership of stream on every path, including invalid arguments. */
PortableFileReadStatus portable_file_read_stream(SDL_IOStream *stream, size_t maximum,
    uint8_t **bytes, size_t *size);
#endif
#endif
