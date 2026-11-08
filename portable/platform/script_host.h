#ifndef PORTABLE_SCRIPT_HOST_H
#define PORTABLE_SCRIPT_HOST_H
/* Lua mod scripts (API version 1). Each mod gets its own sandboxed lua_State,
 * recreated at every race start so a race depends only on its mods and input.
 * Hooks fire from PortableRaceScriptHooks on simulation ticks of the GP stack
 * (GP, Quick Run, VS, Battle; CPU item use included) or of Time Trial; rendering
 * only reads the HUD message. Lua never runs inside portable/core. */
#include <stddef.h>
#include <stdint.h>
#include "gp_race_session.h"
#include "recovered_time_trial_session.h"

#define PORTABLE_SCRIPT_MEMORY_LIMIT (4u*1024u*1024u)
#define PORTABLE_SCRIPT_LOAD_BUDGET 1000000u
#define PORTABLE_SCRIPT_HOOK_BUDGET 100000u
#define PORTABLE_SCRIPT_MESSAGE_CAPACITY 48u
#define PORTABLE_SCRIPT_MESSAGE_MAX_TICKS 600u

typedef struct PortableScriptHost PortableScriptHost;
typedef struct {
    const char *id;     /* mod ID, used in chunk names and messages */
    const char *source; /* Lua text; not NUL-terminated */
    size_t size;
} PortableScriptSource;
typedef enum {
    PORTABLE_SCRIPT_MODE_GP = 0,
    PORTABLE_SCRIPT_MODE_QUICK_RUN,
    PORTABLE_SCRIPT_MODE_BATTLE,
    PORTABLE_SCRIPT_MODE_TIME_TRIAL,
    PORTABLE_SCRIPT_MODE_VS
} PortableScriptMode;

/* Compiles every script once; one that fails to compile or load is named on
 * stderr and stays disabled. NULL only on allocation failure. */
PortableScriptHost *portable_script_host_create(const PortableScriptSource *scripts,size_t count);
void portable_script_host_free(PortableScriptHost *host);
size_t portable_script_host_active_count(const PortableScriptHost *host);
/* First compile/load failure in source order; borrowed until host is freed.
 * Runtime hook failures do not change this creation diagnostic. */
const char *portable_script_host_failed_id(const PortableScriptHost *host);
/* Scripts still running in the current race (errors disable until race end). */
size_t portable_script_host_live_count(const PortableScriptHost *host);
/* Fresh states for a GP/Quick Run/VS/Battle race on an initialized session;
 * attaches the hooks when
 * at least one script loaded and leaves session->script_hooks NULL otherwise.
 * The per-mod race.random stream is seeded from the session and mode. */
int portable_script_host_begin_race(PortableScriptHost *host,PortableGpRaceSession *session,
    PortableScriptMode mode,uint32_t course_id,uint8_t engine_class);
/* The same for a Time Trial session (one kart, 150cc, three mushrooms); call
 * it again after a restart so the scripts start over. */
int portable_script_host_begin_time_trial(PortableScriptHost *host,
    PortableRecoveredTimeTrialSession *session,uint32_t course_id);
void portable_script_host_end_race(PortableScriptHost *host);
/* Read-only presentation: the visible HUD message, or NULL. */
const char *portable_script_host_message(const PortableScriptHost *host);
/* Script item names (green_shell .. lightning) map to 0x1000..0x100c. */
uint16_t portable_script_item_id(const char *name);
const char *portable_script_item_name(uint16_t item);
#endif
