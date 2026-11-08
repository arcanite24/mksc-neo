#ifndef PORTABLE_COURSE_3D_H
#define PORTABLE_COURSE_3D_H

#include "asset_pack.h"
#include "native_image.h"
#include <stddef.h>
#include <stdint.h>

#define PORTABLE_COURSE_3D_TAG(a,b,c,d) ((uint32_t)(a) | ((uint32_t)(b)<<8u) | \
    ((uint32_t)(c)<<16u) | ((uint32_t)(d)<<24u))
#define PORTABLE_COURSE_3D_ABSENT UINT32_MAX
enum { PORTABLE_COURSE_3D_KIND=99, PORTABLE_COURSE_3D_CATALOG_KIND=100,
       PORTABLE_COURSE_3D_TEXTURE_KIND=101, PORTABLE_COURSE_3D_PROFILE_KIND=102,
       PORTABLE_COURSE_3D_TEXTURE_BUNDLE_KIND=108, PORTABLE_COURSE_3D_TABLE_LIMIT=32 };

typedef struct {
    uint32_t tag, count, stride;
    const uint8_t *bytes;
} PortableCourse3DTable;

/* Immutable wire views borrow the final composed pack for their entire life. */
typedef struct {
    const PortableAssetPackView *pack;
    const uint8_t *bytes;
    size_t size;
    uint32_t id, table_count;
    PortableCourse3DTable tables[PORTABLE_COURSE_3D_TABLE_LIMIT];
} PortableCourse3DView;

typedef struct {
    const char *name, *author, *cup_name;
    uint32_t modes, laps, cup_id, cup_order, music_kind, music_id;
    uint32_t preview_id, minimap_id, sky_id, sky_rgba, fog_rgba;
    int32_t fog_near_q16, fog_far_q16;
    uint32_t ambient_rgba;
    int32_t sun_q30[3];
    uint32_t sun_rgba;
    int32_t gravity_q16;
    uint32_t physics_profile_id, feature_bits, property_first, property_count;
} PortableCourse3DMeta;
typedef struct { int32_t position_q16[3], normal_q30[3], uv_q16[2]; } PortableCourse3DVertex;
typedef struct { uint32_t vertices[3], material; } PortableCourse3DTriangle;
typedef struct {
    uint32_t textures[4], base_rgba, emissive_rgba;
    int32_t metallic_q16, roughness_q16, alpha_cutoff_q16;
    uint32_t flags, property_first, property_count;
} PortableCourse3DMaterial;
typedef struct
{
    int32_t position_q16[3], quaternion_q30[4];
} PortableCourse3DGrid;
typedef struct
{
    int32_t position_q16[3], radius_q16;
    uint32_t yaw_turns, respawn_ticks, property_first, property_count;
} PortableCourse3DItem;
/* ITEM rows borrow immutable content; failure preserves out. */
int portable_course_3d_item(const PortableCourse3DView *, uint32_t, PortableCourse3DItem *);
typedef struct { uint32_t course_id, cup_id, slot, modes, sort_key; } PortableCourse3DCatalog;

typedef struct { uint32_t vertices[3], surface, flags, respawn, gravity_zone, owner_object; } PortableCourse3DCollision;
typedef struct {
    uint32_t type, flags;
    int32_t friction_q16, speed_ratio_q16, rolling_resistance_q16, restitution_q16;
    int32_t boost_q16, launch_q16, gravity_ratio_q16;
    uint32_t event_id, property_first, property_count;
    int32_t jump_max_distance_q16; /*Decoded property, absent0; not a wire field. */
    int32_t cannon_target_q16[3], cannon_heading_q16[3]; /* Decoded type 9 properties. */
    uint32_t one_sided, surface_gravity; /* Decoded `mksc:one-sided`, `mksc:surface-gravity`. */
    uint32_t surface_class; /* Decoded `mksc:surface-class` 1..10 (MK8 class); 0 absent. */
    uint32_t trick;         /* Decoded `mksc:trick`: 1 true, 2 false, 0 absent. */
    uint32_t glide;         /* Decoded `mksc:glide`: a glide ramp. */
    uint32_t sound_surface; /* Decoded `mksc:sound-surface` 1..63 (sound set surface class); 0 absent. */
    uint32_t splash;        /* Decoded `mksc:splash`: wheels throw water, not dust (presentation). */
} PortableCourse3DSurface;
typedef struct {
    int32_t center_q16[3], normal_q30[3], right_q30[3], half_width_q16, half_height_q16;
    uint32_t key_group, stage, edge_first, edge_count, flags;
} PortableCourse3DCheckpoint;
typedef struct {
    int32_t position_q16[3], quaternion_q30[4];
    uint32_t checkpoint;
    int32_t radius_q16;
    uint32_t flags, route;
} PortableCourse3DRespawn;
typedef struct
{
    uint32_t kind, node_first, node_count, start_node, flags, property_first, property_count;
} PortableCourse3DRoute;
/* CAMR intro shot `order` (integer mksc:intro-order, 0 first): the camera
 * travels its kind-2 route (or holds its position) for mksc:camera-ticks
 * while its aim eases from mksc:camera-target-begin to -end and its vertical
 * field of view from the row's to mksc:camera-fov-end (1/65536 turns). */
typedef struct
{
    int32_t position_q16[3], target_q16[2][3];
    uint32_t route, ticks, fov_turns[2];
} PortableCourse3DIntroShot;
typedef struct
{
    int32_t position_q16[3], radius_q16, speed_q16;
    uint32_t edge_first, edge_count, flags;
} PortableCourse3DRouteNode;
int portable_course_3d_route(const PortableCourse3DView *, uint32_t, PortableCourse3DRoute *);
int portable_course_3d_intro_shot(const PortableCourse3DView *view, uint32_t order, PortableCourse3DIntroShot *out);
int portable_course_3d_route_node(const PortableCourse3DView *, uint32_t,
                                  PortableCourse3DRouteNode *);
int portable_course_3d_route_edge(const PortableCourse3DView *, uint32_t, uint32_t *);

typedef enum
{
    PORTABLE_COURSE_3D_PROPERTY_INTEGER,
    PORTABLE_COURSE_3D_PROPERTY_FIXED,
    PORTABLE_COURSE_3D_PROPERTY_BOOLEAN,
    PORTABLE_COURSE_3D_PROPERTY_STRING,
    PORTABLE_COURSE_3D_PROPERTY_ASSET,
    PORTABLE_COURSE_3D_PROPERTY_OBJECT,
    PORTABLE_COURSE_3D_PROPERTY_ROUTE,
    PORTABLE_COURSE_3D_PROPERTY_VECTOR
} PortableCourse3DPropertyType;
typedef struct
{
    const char *key;
    PortableCourse3DPropertyType type;
    union
    {
        int32_t integer, fixed_q16;
        uint32_t boolean, object, route;
        const char *string;
        struct
        {
            uint32_t kind, id;
        } asset;
        int32_t vector_q16[3];
    } value;
} PortableCourse3DProperty;
/* Keys/strings borrow the course pack. Missing/invalid values preserve out.
 * Find searches only the supplied owner span; the last matching key wins. */
int portable_course_3d_property_at(const PortableCourse3DView *, uint32_t index,
                                   PortableCourse3DProperty *out);
int portable_course_3d_property_find(const PortableCourse3DView *, uint32_t first, uint32_t count,
                                     const char *key, PortableCourse3DProperty *out);
int portable_course_3d_collision_vertex(const PortableCourse3DView *, uint32_t, int32_t position_q16[3]);
int portable_course_3d_collision(const PortableCourse3DView *, uint32_t, PortableCourse3DCollision *);
int portable_course_3d_surface(const PortableCourse3DView *, uint32_t, PortableCourse3DSurface *);
/* Kind-1 ZONE rows are MK8 gravity-path points and kind-2 rows glide-path
 * points: center, gravity (its negation is the path's up), radius (the
 * largest half-extent), the next point's zone of the same kind (integer
 * mksc:gravity-next or mksc:glide-next; absent ends the chain) and a
 * boolean flag (mksc:anti-gravity, or mksc:glide-cannon). 1 a valid point,
 * 0 another zone kind or a malformed point. */
typedef struct
{
    int32_t position_q16[3], gravity_q16[3], radius_q16;
    uint32_t kind, next, anti_gravity;
} PortableCourse3DGravityPoint;
int portable_course_3d_gravity_point(const PortableCourse3DView *, uint32_t zone, PortableCourse3DGravityPoint *);
/* Kind-3 ZONE rows are positional sound emitters: center, audible radius (the
 * largest half-extent), the song (integer mksc:sound, a sound-set song ID
 * 1..65535) and its restart period in ticks (integer mksc:sound-period,
 * 0..65535; absent or 0 restarts when the song ends). 1 a valid emitter,
 * 0 another zone kind or a malformed emitter. */
typedef struct
{
    int32_t position_q16[3], radius_q16;
    uint16_t song, period;
} PortableCourse3DSoundZone;
int portable_course_3d_sound_zone(const PortableCourse3DView *, uint32_t zone, PortableCourse3DSoundZone *);
/* Kind-4 ZONE rows are particle emitters: center, orientation (quaternion
 * Q30: local x right, y forward, z up) and integer mksc:emitter, the ID of
 * an emitter row in the course's effect set (META mksc:fx-set). 1 a valid
 * emitter, 0 another zone kind or a malformed one. */
typedef struct
{
    int32_t position_q16[3], quaternion_q30[4];
    uint32_t emitter;
} PortableCourse3DEmitterZone;
int portable_course_3d_emitter_zone(const PortableCourse3DView *, uint32_t zone, PortableCourse3DEmitterZone *);
int portable_course_3d_checkpoint(const PortableCourse3DView *, uint32_t, PortableCourse3DCheckpoint *);
int portable_course_3d_checkpoint_edge(const PortableCourse3DView *, uint32_t, uint32_t *);
int portable_course_3d_respawn(const PortableCourse3DView *, uint32_t, PortableCourse3DRespawn *);

/* Failure empties out and returns a course/table/row diagnostic when provided. */
int portable_course_3d_open(const PortableAssetPackView *pack, uint32_t id,
    PortableCourse3DView *out, char *error, size_t error_capacity);
int portable_course_3d_validate_pack(const PortableAssetPackView *pack,
    char *error, size_t error_capacity);
size_t portable_course_3d_catalog_count(const PortableAssetPackView *pack);
int portable_course_3d_catalog_at(const PortableAssetPackView *pack, size_t index,
    PortableCourse3DCatalog *out, char *error, size_t error_capacity);
const PortableCourse3DTable *portable_course_3d_table(const PortableCourse3DView *view, uint32_t tag);
int portable_course_3d_meta(const PortableCourse3DView *view, PortableCourse3DMeta *out);
/* Class0/1/2 selects optional50/100/150cc META asset properties. Missing
 * mappings use the base; malformed declarations reject, preserving out. */
int portable_course_3d_physics_profile(const PortableCourse3DView *, unsigned engine_class,
                                       uint32_t *out, char *error, size_t capacity);
/* Optional META mksc:item-rules asset; absent returns zero ID. Every declared
 * reference is checked, including overridden declarations. Failure preserves out. */
int portable_course_3d_item_rules(const PortableCourse3DView *,uint32_t *out,char*,size_t);
/* The rules a mode races with: Quick Run and VS (modes 4 and 8) take
 * mksc:item-rules-vs when the course declares it, else mksc:item-rules. */
int portable_course_3d_item_rules_mode(const PortableCourse3DView *,uint32_t mode,uint32_t *out,char*,size_t);
int portable_course_3d_has_physics_profile(const PortableCourse3DView *, uint32_t profile_id);
int portable_course_3d_vertex(const PortableCourse3DView *view, uint32_t index,
                              PortableCourse3DVertex *out);
int portable_course_3d_triangle(const PortableCourse3DView *view, uint32_t index, PortableCourse3DTriangle *out);
int portable_course_3d_material(const PortableCourse3DView *view, uint32_t index, PortableCourse3DMaterial *out);
int portable_course_3d_grid(const PortableCourse3DView *view, uint32_t index, PortableCourse3DGrid *out);
/* A kind-101 image, else an entry of a META mksc:texture-bundle (kind 108). */
int portable_course_3d_texture(const PortableCourse3DView *view, uint32_t id, PortableNativeImage *out);
/* META minimap art and the world XY rectangle (META vector properties
 * mksc:minimap-low/high, Q16) its edges span; 0 when META has none. */
typedef struct
{
    PortableNativeImage image;
    int32_t low_q16[2], high_q16[2];
} PortableCourse3DMinimap;
int portable_course_3d_minimap(const PortableCourse3DView *view, PortableCourse3DMinimap *out);
/* Integer mksc:present-in on a MATS or SURF row limits its faces or
 * collision to some races: bit mode slot * 4 + engine class (slots 0 Grand
 * Prix, 1 Time Trial, 2 Quick Run, 3 VS, 4 Battle; classes 0..2 50/100/150cc).
 * Rows without it are always present. ALL (previews, exploration) keeps
 * every row. */
#define PORTABLE_COURSE_3D_PRESENT_ALL UINT32_MAX
uint32_t portable_course_3d_presence(uint32_t mode, unsigned engine_class);
int portable_course_3d_present(const PortableCourse3DView *view, uint32_t property_first,
                               uint32_t property_count, uint32_t presence);
/* META kart scale and chase framing, for content made at another game's
 * proportions. Fixed mksc:kart-scale (default 1, 1/8..4) sizes every kart
 * against the stock 3D kart (0.5 m collision radius): collision radius and
 * ride height, drawn kart and driver, shadow, effects and carried items.
 * Vectors mksc:camera-eye and mksc:camera-target (metres from the kart's
 * ground foot: x right, which must be 0, y forward, z up; both or neither)
 * place the resting chase eye and its aim; integer mksc:camera-fov is the
 * vertical field of view in 1/65536 turns (10..120 degrees). Absent camera
 * values keep the stock rig, scaled by the kart scale, and its field of view.
 * Malformed values fail with a META diagnostic and preserve out. */
typedef struct
{
    int32_t kart_scale_q16;
    int32_t eye_q16[3], target_q16[3];
    uint32_t fov_turns; /* 0: stock */
    uint32_t has_rig;
} PortableCourse3DChase;
int portable_course_3d_chase(const PortableCourse3DView *view, PortableCourse3DChase *out,
                             char *error, size_t capacity);
/* VCOL RGBA (R in the high byte) multiplies the material; white without VCOL. */
uint32_t portable_course_3d_vertex_rgba(const PortableCourse3DView *view, uint32_t index);
/* TXAN row: kind 0 steps TXSR Q16 texture matrices {a,b,c,d,tx,ty} with
 * u'=a*u+c*v+tx, v'=b*u+d*v+ty (count 1 or frame_count); kind 1 swaps the base
 * texture at TXPT {frame, texture} keys. Both loop over frame_count ticks. */
typedef struct { uint32_t material, kind, frame_count, first, count; } PortableCourse3DTextureAnimation;
int portable_course_3d_texture_animation(const PortableCourse3DView *view, uint32_t index,
                                         PortableCourse3DTextureAnimation *out);
int portable_course_3d_texture_srt(const PortableCourse3DView *view, uint32_t index, int32_t out_q16[6]);
int portable_course_3d_texture_key(const PortableCourse3DView *view, uint32_t index, uint32_t *frame,
                                   uint32_t *texture);

#endif
