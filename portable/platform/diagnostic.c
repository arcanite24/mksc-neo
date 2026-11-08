#include "../app/diagnostic.h"

#include <stdarg.h>
#include <stdio.h>

int portable_diagnostic_printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const int written = vfprintf(stderr, format, args);
    va_end(args);
    return written;
}
