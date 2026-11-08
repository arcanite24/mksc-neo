#ifndef PORTABLE_STARTUP_TRACE_H
#define PORTABLE_STARTUP_TRACE_H
#include <stdint.h>
uint64_t portable_startup_clock(void);
void portable_startup_begin(void);
void portable_startup_phase(const char *phase, uint64_t since);
void portable_startup_mark(const char *phase);
#endif
