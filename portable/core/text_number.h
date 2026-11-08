#ifndef PORTABLE_TEXT_NUMBER_H
#define PORTABLE_TEXT_NUMBER_H

#include <stdint.h>

/* Parse within [*cursor,end), skipping ASCII whitespace. Bases 2..36 are
 * supported; base 0 recognizes decimal, leading-zero octal and 0x hex.
 * Explicit base 16 also accepts 0x. A leading + is accepted; only signed parsing
 * accepts -. Stop at the first non-digit, leaving delimiter validation to the
 * caller. Failure leaves both cursor and output unchanged. */
int portable_text_u64(const char **cursor, const char *end, unsigned base, uint64_t maximum,
                      uint64_t *out);
int portable_text_i64(const char **cursor, const char *end, unsigned base, int64_t minimum,
                      int64_t maximum, int64_t *out);

#endif
