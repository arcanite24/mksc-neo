#ifndef PORTABLE_MKDS_MAP_H
#define PORTABLE_MKDS_MAP_H

/* Mario Kart DS course collision (KCL) and map data (NKMD). Layouts follow
 * MarioKartToolbox (MIT) and the MKDS decomp headers (read-only). Readers
 * decode into native arrays in DS axes and fx32 map units (x east, y up,
 * z south); course conversion maps them onto engine tables. Malformed data
 * fails naming the file and field. */

#include "rom_import_internal.h"

/* Collision types: attribute bits 8-12. */
enum
{
    MKDS_KCL_ROAD = 0,
    MKDS_KCL_SLIPPERY = 1,
    MKDS_KCL_WEAK_OFF_ROAD = 2,
    MKDS_KCL_OFF_ROAD = 3,
    MKDS_KCL_SOUND_TRIGGER = 4,
    MKDS_KCL_HEAVY_OFF_ROAD = 5,
    MKDS_KCL_SLIPPERY_2 = 6,
    MKDS_KCL_BOOST = 7,
    MKDS_KCL_WALL = 8,
    MKDS_KCL_INVISIBLE_WALL = 9,
    MKDS_KCL_OUT_OF_BOUNDS = 10,
    MKDS_KCL_FALL = 11,
    MKDS_KCL_JUMP = 12,
    MKDS_KCL_ROAD_NO_DRIVERS = 13,
    MKDS_KCL_WALL_NO_DRIVERS = 14,
    MKDS_KCL_CANNON = 15,
    MKDS_KCL_EDGE_WALL = 16,
    MKDS_KCL_FALLS_WATER = 17,
    MKDS_KCL_BOOST_MIN_SPEED = 18,
    MKDS_KCL_LOOP = 19,
    MKDS_KCL_SPECIAL_ROAD = 20,
    MKDS_KCL_WALL_3 = 21,
    MKDS_KCL_RECALCULATE_ROUTE = 22,
};
#define MKDS_KCL_TYPE(attribute) (((attribute) >> 8) & 31u)
#define MKDS_KCL_VARIANT(attribute) (((attribute) >> 5) & 7u)

typedef struct MkdsKclTriangle
{
    int32_t vertex[3][3]; /* fx32; counter-clockwise seen from the normal side */
    int32_t normal[3];    /* fx16 face normal */
    uint32_t attribute;
} MkdsKclTriangle;

typedef struct MkdsKcl
{
    MkdsKclTriangle *triangles;
    size_t count;
} MkdsKcl;

MkdsKcl mkds_kcl(RiCtx *ctx, RiData file, const char *name);

typedef struct MkdsObject
{
    int32_t position[3], rotation[3], scale[3]; /* rotation in fx32 degrees */
    uint32_t id;
    int32_t route;
    uint32_t time_trial; /* shown in Time Trial */
    uint32_t settings[4]; /* per-MapObj parameters */
} MkdsObject;

/* KTPS, KTPJ, KTP2, KTPC and KTPM entries: a pose plus two indices. */
typedef struct MkdsPose
{
    int32_t position[3], rotation[3];
    int32_t index, enemy_point, item_point; /* KTPJ only: point IDs */
} MkdsPose;

typedef struct MkdsCheckpoint
{
    int32_t left[2], right[2]; /* x, z */
    int32_t key, respawn;      /* key -1: none */
    int32_t goto_section, start_section;
} MkdsCheckpoint;

/* CPAT, IPAT, EPAT (three links to paths) and MEPA (eight links to points):
 * a run of points. */
#define MKDS_PATH_LINKS 8
typedef struct MkdsPath
{
    uint32_t start, length;
    int32_t next[MKDS_PATH_LINKS], previous[MKDS_PATH_LINKS]; /* -1: none */
} MkdsPath;

typedef struct MkdsPoint
{
    int32_t position[3], radius;
} MkdsPoint;

/* PATH: a run of POIT points that objects and cameras travel. */
typedef struct MkdsRail
{
    uint32_t start, length, loop;
} MkdsRail;

/* CAME: fov in degrees (half the vertical view), ticks at 60 Hz. */
typedef struct MkdsCamera
{
    int32_t position[3], target[2][3];
    uint32_t fov[2], type, duration, intro; /* intro 1: first top-screen intro camera */
    int32_t rail, next;                     /* -1: none */
} MkdsCamera;

/* NKM STAG lighting and fog. The game passes fog_shift and fog_distance >> 9
 * to G3X_SetFog and builds the 32-entry density table by fog_mode (0 linear,
 * 1 ease-in, 2 ease-out); light_colors are the kart light colours a KCL
 * prism's attribute bits 0-1 select. Colours are BGR555. */
typedef struct MkdsStage
{
    uint32_t course_id, fog_enabled, fog_mode, fog_shift;
    int32_t fog_distance; /* fx32 view depth */
    uint32_t fog_color, light_colors[4];
} MkdsStage;

typedef struct MkdsMap
{
    uint32_t version, laps;
    MkdsStage stage;
    MkdsObject *objects;
    size_t object_count;
    MkdsPose *starts, *respawns, *cannons; /* cannons: KTPC targets */
    size_t start_count, respawn_count, cannon_count;
    MkdsCheckpoint *checkpoints;
    size_t checkpoint_count;
    MkdsPath *checkpoint_paths, *item_paths, *enemy_paths;
    size_t checkpoint_path_count, item_path_count, enemy_path_count;
    MkdsPoint *item_points, *enemy_points; /* enemy: EPOI, or MEPO on battle stages */
    int enemy_links_points;                /* MEPA: links name points, not paths */
    size_t item_point_count, enemy_point_count;
    MkdsRail *rails;
    int32_t (*rail_points)[3];
    int32_t *rail_durations; /* POIT: frames to the next point */
    size_t rail_count, rail_point_count;
    MkdsCamera *cameras;
    size_t camera_count;
} MkdsMap;

MkdsMap mkds_nkm(RiCtx *ctx, RiData file, const char *name);

#endif
