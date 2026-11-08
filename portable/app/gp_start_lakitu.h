#ifndef PORTABLE_GP_START_LAKITU_H
#define PORTABLE_GP_START_LAKITU_H

#include <stdint.h>
#include "../core/finish_lakitu.h"

enum { PORTABLE_GP_START_LAKITU_BOB_BYTES=128u,
       /* EF310 starts ascent1710 and waits20 actor turns before50E78. */
       PORTABLE_GP_START_LAKITU_LABEL_ENABLE_FRAME=1710u+20u };

/* Displayed course25 opening OAM, from the preceding actor turn.
 * EF050 contains sixteen little-endian signed x/y offset pairs.
 * Returns 1 for a submitted lamp, 0 after source culling, -1 on misuse.
 * This pure presentation query never advances game or actor state. */
int portable_gp_start_lakitu_y(uint32_t intro_frame,
    const uint8_t bob_offsets[PORTABLE_GP_START_LAKITU_BOB_BYTES],
    const int32_t sine[1024],int16_t *cell_y);

/* Bounded2749 continuation; legacy2629 rejection remains unchanged. */
int portable_gp_start_lakitu_y_hit_continuation(uint32_t intro_frame,
    const uint8_t bob_offsets[PORTABLE_GP_START_LAKITU_BOB_BYTES],
    const int32_t sine[1024],int16_t *cell_y);

typedef enum
{
    PORTABLE_GP_RECOVERY_LAKITU_IDLE,
    PORTABLE_GP_RECOVERY_LAKITU_DESCENT,
    PORTABLE_GP_RECOVERY_LAKITU_BOB,
    PORTABLE_GP_RECOVERY_LAKITU_RETRIEVE,
    PORTABLE_GP_RECOVERY_LAKITU_LIFT,
    PORTABLE_GP_RECOVERY_LAKITU_DROP,
    PORTABLE_GP_RECOVERY_LAKITU_EXIT,
    PORTABLE_GP_RECOVERY_LAKITU_CANCEL_EXIT,
} PortableGpRecoveryLakituCallback;

typedef enum
{
    PORTABLE_GP_RECOVERY_SCRIPT_NONE,
    PORTABLE_GP_RECOVERY_SCRIPT_DESCENT_HOLD, /* EF8E0 */
    PORTABLE_GP_RECOVERY_SCRIPT_RETRIEVE_HOLD, /* EF930 */
    PORTABLE_GP_RECOVERY_SCRIPT_LIFT_WAIT, /* EF810, decoded next turn */
    PORTABLE_GP_RECOVERY_SCRIPT_LIFT_INSTALL, /* EF818, after10-turn wait */
    PORTABLE_GP_RECOVERY_SCRIPT_LIFT_HOLD, /* EF840 */
    PORTABLE_GP_RECOVERY_SCRIPT_DROP_INSTALL, /* EF9A0 */
    PORTABLE_GP_RECOVERY_SCRIPT_DROP_HOLD, /* EF9C8 */
    PORTABLE_GP_RECOVERY_SCRIPT_EXIT_INSTALL, /* EF9E0 */
    PORTABLE_GP_RECOVERY_SCRIPT_EXIT_WAIT, /* EFA10 */
    PORTABLE_GP_RECOVERY_SCRIPT_HIDE, /* EF898: jump stops this decode pass */
    PORTABLE_GP_RECOVERY_SCRIPT_HIDDEN_HOLD, /* EF8A8 */
} PortableGpRecoveryLakituScript;

enum { PORTABLE_GP_LAKITU_WARNING=6u, PORTABLE_GP_LAKITU_WARNING_EXIT=7u,
       PORTABLE_GP_LAKITU_RESET=8u, PORTABLE_GP_LAKITU_FINISH=9u };

typedef struct
{
    int16_t x, y;
    uint16_t visible;
    uint16_t wait;
    uint16_t finish_turn, inverse_q8; /* D000 callback age and source inverse axis. */
    uint16_t warning_turn, cleared_requests; /* wrong-way script clock and deferred A4 clear */
    int32_t counter; /* actor7C; distinct from script wait */
    uint8_t art_bank; /* FEA0 bank19/20 */
    uint8_t flags; /* script.c continuation/first-call bits */
    uint8_t phase; /* 0 idle,1..5 retrieval E000..E004,6 warning,7 warning exit,8 reset */
    uint8_t retrieval_effect_started;
    uint8_t drop_started, coin_loss_requested;
    int32_t exit_velocity_q16; /* actor70, separate from script wait/counter */
    PortableGpRecoveryLakituCallback callback;
    PortableGpRecoveryLakituScript script;
} PortableGpRecoveryLakituState;

/* D000 consumes the final-lap marker before the driver pass. Missing rules
 * retain old-pack behavior without fabricating an animation. */
int portable_gp_finish_lakitu_consume(PortableGpRecoveryLakituState *state,
    uint16_t *lap_markers, const PortableGpFinishLakituRules *rules);
int portable_gp_finish_lakitu_tick(PortableGpRecoveryLakituState *state,
    const PortableGpFinishLakituRules *rules,
    const uint8_t bob_offsets[PORTABLE_GP_START_LAKITU_BOB_BYTES],
    const int32_t sine[1024]);

/* Pre-driver51014 request consumption only. Actor script/wait/callback are
 * advanced separately after the driver pass; no render advances either. */
int portable_gp_recovery_lakitu_consume(PortableGpRecoveryLakituState *state,
    uint16_t *requests, uint32_t driver_flags);

/* 51014 consumes requests from the PRECEDING driver turn before301A8 and
 * that installed callback. F9E4 changes the script for the following turn;
 * B64 changes driver height after C4C0 in49850. Returns1 on the recovered branch,
 * -1 for an unsupported request, without partial state writes. Exit
 * retains pending requests until idle; retrieval during a drop is unsupported. Bob data/sine are required only
 * for the water descent/bob callbacks.
 * Call once per simulation turn, never from rendering. */
int portable_gp_recovery_lakitu_tick(PortableGpRecoveryLakituState *state,
    uint16_t *requests, uint32_t driver_flags, int16_t prior_projected_y,
    int32_t *height_q16, uint16_t *packed_effect_72,
    const uint8_t bob_offsets[PORTABLE_GP_START_LAKITU_BOB_BYTES],
    const int32_t sine[1024]);

/* F674 signed culling and three source cells, affine261. The28-byte result
 * has bank0/1, count, three attr triplets and one affine matrix. Query only;
 * the host chooses queued/current or displayed/previous actor state. */
uint16_t portable_gp_lakitu_source_state(const PortableGpRecoveryLakituState *state);
uint32_t portable_gp_lakitu_image(uint8_t art_bank);
uint16_t portable_gp_lakitu_scale(const PortableGpRecoveryLakituState *state);

int portable_gp_recovery_lakitu_record(const PortableGpRecoveryLakituState *state,
    const uint8_t source_cells[20],uint8_t record[28]);

#endif
