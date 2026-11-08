#ifndef PORTABLE_RACE_SCRIPT_HOOKS_H
#define PORTABLE_RACE_SCRIPT_HOOKS_H

#include <stddef.h>
#include <stdint.h>

/* Optional mod-script observer, owned by the platform host and shared by the
 * GP/Quick Run/Battle stack and Time Trial. after_tick runs once at the end
 * of every successful simulation tick. item_use runs when racer `racer`
 * (0-based roster index; Time Trial has only racer 0) activates the item in
 * its slot and returns the item to use: the same one, a replacement, or zero
 * to cancel and keep the item. Both run on simulation ticks, never from
 * rendering; a NULL pointer leaves the race untouched. Not race state. */
typedef struct
{
    void *context;
    void (*after_tick)(void *context);
    uint16_t (*item_use)(void *context, size_t racer, uint16_t item);
} PortableRaceScriptHooks;

#endif
