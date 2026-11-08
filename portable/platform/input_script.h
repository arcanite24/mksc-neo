#ifndef MKSC_PORTABLE_INPUT_SCRIPT_H
#define MKSC_PORTABLE_INPUT_SCRIPT_H

#include <stddef.h>

#include "input_replay.h"
#include "mesh_kart.h"

enum
{
    PORTABLE_INPUT_SCRIPT_OK = 0,
    PORTABLE_INPUT_SCRIPT_INVALID_ARGUMENT = 1,
    PORTABLE_INPUT_SCRIPT_INVALID_TOKEN = 2,
    PORTABLE_INPUT_SCRIPT_CAPACITY = 3,
};

/*
 * Decode one caller-owned semantic control script into source-mapped replay
 * frames. Each token is one native driver frame:
 *
 *   . coast, A accelerate, B brake, l left, r right,
 *   L accelerate+left, R accelerate+right.
 */
int portable_input_script_parse(const char *script,
                                PortableInputFrame *frames,
                                size_t capacity,
                                size_t *out_count);

#define PORTABLE_MESH_INPUT_SEGMENT_CAPACITY 32u
#define PORTABLE_MESH_INPUT_SEGMENT_TICKS 4096u

typedef struct
{
    uint32_t first_tick, count;
    PortableMeshKartInput input;
} PortableMeshInputSegment;

typedef struct
{
    PortableMeshInputSegment segments[PORTABLE_MESH_INPUT_SEGMENT_CAPACITY];
    size_t count;
} PortableMeshInputSegments;

/* START:COUNT:STEER:BUTTONS separated by semicolons, decimal only. Starts are
 * preturn race ticks (countdown included); segments are ordered/nonoverlapping,
 * at most32 segments/4096 total turns. Buttons are Accel1/Brake2/Hop4 only.
 * Parse failures preserve the entire caller-owned output. */
int portable_mesh_input_segments_parse(const char *text, PortableMeshInputSegments *out);
/* 1 selected,0 outside segments,-1 invalid; output preserved on0/-1.
 * Sampling is immutable and permits Retry/time rollback without cursor state. */
int portable_mesh_input_segments_sample(const PortableMeshInputSegments *segments,
                                         uint32_t tick, PortableMeshKartInput *out);

/* NULL selects every race; otherwise exactly one GP ordinal digit0..3.
 * UINT32_MAX denotes unscoped/non-GP. Failures preserve outputs. */
int portable_mesh_input_race_parse(const char *text, uint32_t *out);
int portable_mesh_input_segments_sample_race(const PortableMeshInputSegments *segments,
    uint32_t selected_race, uint32_t current_race, uint32_t tick, PortableMeshKartInput *out);

#endif
