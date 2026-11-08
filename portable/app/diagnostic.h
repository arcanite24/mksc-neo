#ifndef PORTABLE_DIAGNOSTIC_H
#define PORTABLE_DIAGNOSTIC_H

/* The backend supplies this sink. Diagnostics never alter simulation state. */
int portable_diagnostic_printf(const char *format, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

#endif
