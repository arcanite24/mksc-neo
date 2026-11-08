#include "text_number.h"

#include <stddef.h>

static int ascii_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

static unsigned digit_value(char c)
{
    if (c >= '0' && c <= '9')
        return (unsigned)(c - '0');
    if (c >= 'a' && c <= 'z')
        return (unsigned)(c - 'a') + 10u;
    if (c >= 'A' && c <= 'Z')
        return (unsigned)(c - 'A') + 10u;
    return 36u;
}

static int magnitude(const char **cursor, const char *end, unsigned base, uint64_t maximum,
                     uint64_t *out)
{
    const char *at = *cursor;
    if (base != 0u && (base < 2u || base > 36u))
        return 0;
    if (at == end)
        return 0;
    if ((base == 0u || base == 16u) && at + 1 != end && at[0] == '0' &&
        (at[1] == 'x' || at[1] == 'X'))
    {
        base = 16u;
        at += 2;
    }
    else if (base == 0u)
        base = *at == '0' ? 8u : 10u;
    const char *first = at;
    uint64_t value = 0u;
    while (at != end)
    {
        const unsigned digit = digit_value(*at);
        if (digit >= base)
            break;
        if (digit > maximum || value > (maximum - digit) / base)
            return 0;
        value = value * base + digit;
        ++at;
    }
    if (at == first)
        return 0;
    *cursor = at;
    *out = value;
    return 1;
}

int portable_text_u64(const char **cursor, const char *end, unsigned base, uint64_t maximum,
                      uint64_t *out)
{
    if (cursor == NULL || *cursor == NULL || end == NULL || out == NULL || *cursor > end)
        return 0;
    const char *at = *cursor;
    while (at != end && ascii_space(*at))
        ++at;
    if (at != end && *at == '+')
        ++at;
    uint64_t value;
    if (!magnitude(&at, end, base, maximum, &value))
        return 0;
    *cursor = at;
    *out = value;
    return 1;
}

int portable_text_i64(const char **cursor, const char *end, unsigned base, int64_t minimum,
                      int64_t maximum, int64_t *out)
{
    if (cursor == NULL || *cursor == NULL || end == NULL || out == NULL || *cursor > end ||
        minimum > maximum)
        return 0;
    const char *at = *cursor;
    while (at != end && ascii_space(*at))
        ++at;
    const int negative = at != end && *at == '-';
    if (at != end && (*at == '+' || *at == '-'))
        ++at;
    const uint64_t limit = negative ? (minimum < 0 ? (uint64_t)-(minimum + 1) + 1u : 0u)
                                    : (maximum >= 0 ? (uint64_t)maximum : 0u);
    uint64_t parsed;
    if (!magnitude(&at, end, base, limit, &parsed))
        return 0;
    const int64_t value =
        negative ? (parsed > (uint64_t)INT64_MAX ? INT64_MIN : -(int64_t)parsed) : (int64_t)parsed;
    if (value < minimum || value > maximum)
        return 0;
    *cursor = at;
    *out = value;
    return 1;
}
