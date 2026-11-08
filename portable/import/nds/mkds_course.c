#include "mkds_course.h"
#include "nitro_2d.h"
#include "fixed_3d.h"
#include "mkds_map.h"
#include "mkds_model.h"
#include "mkds_fx.h"
#include "mkds_sound.h"
#include "mkds_physics.h"
#include "mesh_item_rules.h"
#include "mesh_kart_rules.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

enum
{
    KIND_COURSE = 99,
    KIND_CATALOG = 100,
    KIND_RIG = 109,
    MODE_GP = 1,
    MODE_TT = 2,
    MODE_QUICK = 4,
    MODE_VS = 8,
    MODE_BATTLE = 16,
};

typedef struct Course
{
    MkdsModel m;
    const MkdsCourseSource *source;
    RiBuf *platform_faces; /* moving MapObj faces at rest: 9 corner words, then the OBJS owner */
    uint32_t platform_face_count, object_mesh_count;
    RiBuf *solid_walls; /* SOLID MapObj prism faces: 9 corner words */
    uint32_t solid_wall_count;
    const MkdsObject *chained; /* the Chain Chomp placement whose Chomp is being placed at rest */
    /* The shadow volume of the family being placed (see object_shadow). */
    struct
    {
        uint32_t material; /* kind-97 ID; 0 without one */
        int32_t low[3], high[3]; /* volume bounds, DS model units */
    } shadow;
    uint32_t sway_link;        /* a Pokey segment being placed: its link index + 1 */
    int32_t sway_lift;         /* and its height over the placement, Q16 metres */
} Course;

static uint32_t collision_vertex(Course *c, const int32_t position[3])
{
    uint8_t key[12];
    for (unsigned axis = 0; axis < 3; ++axis)
        mkds_put_word(key + axis * 4, (uint32_t)position[axis]);
    const uint32_t found = mkds_weld(&c->m, 1, key, sizeof(key), MKDS_T_COLV);
    if (found != MKDS_ABSENT)
        return found;
    ri_put(c->m.ctx, c->m.tables[MKDS_T_COLV].bytes, key, 12);
    return c->m.tables[MKDS_T_COLV].count++;
}

/* ---- Gameplay ----------------------------------------------------------------- */

/* Modern adaptation of each KCL collision type: engine surface type and Q16
 * friction, speed ratio, boost and launch. Types drivers never touch are
 * dropped; unknown types fail. */
enum
{
    SURFACE_ROAD = 0,
    SURFACE_OFF_ROAD = 1,
    SURFACE_SLIPPERY = 2,
    SURFACE_WALL = 3,
    SURFACE_BOOST = 4,
    SURFACE_JUMP = 5,
    SURFACE_WATER = 6,
    SURFACE_OUT_OF_BOUNDS = 8,
    SURFACE_CANNON = 9,
    SURFACE_NONE = 255,
    MKDS_KCL_TYPE_COUNT = 23,
    COLLISION_IGNORES_DRIVERS = 1u << 4,
    CANNON_SPEED = 72 << 16, /* metres/second of cannon flight */
};
static const struct
{
    uint8_t type;
    uint32_t friction, speed_ratio, boost, launch;
} kcl_surfaces[MKDS_KCL_TYPE_COUNT] = {
    [MKDS_KCL_ROAD] = {SURFACE_ROAD, 65536, 65536, 0, 0},
    [MKDS_KCL_SLIPPERY] = {SURFACE_SLIPPERY, 19661, 65536, 0, 0},
    [MKDS_KCL_WEAK_OFF_ROAD] = {SURFACE_OFF_ROAD, 58982, 52429, 0, 0},
    [MKDS_KCL_OFF_ROAD] = {SURFACE_OFF_ROAD, 52429, 39322, 0, 0},
    [MKDS_KCL_SOUND_TRIGGER] = {SURFACE_NONE, 0, 0, 0, 0},
    [MKDS_KCL_HEAVY_OFF_ROAD] = {SURFACE_OFF_ROAD, 45875, 26214, 0, 0},
    [MKDS_KCL_SLIPPERY_2] = {SURFACE_SLIPPERY, 19661, 65536, 0, 0},
    [MKDS_KCL_BOOST] = {SURFACE_BOOST, 65536, 65536, 196608, 0},
    [MKDS_KCL_WALL] = {SURFACE_WALL, 65536, 65536, 0, 0},
    [MKDS_KCL_INVISIBLE_WALL] = {SURFACE_WALL, 65536, 65536, 0, 0},
    [MKDS_KCL_OUT_OF_BOUNDS] = {SURFACE_OUT_OF_BOUNDS, 65536, 65536, 0, 0},
    [MKDS_KCL_FALL] = {SURFACE_OUT_OF_BOUNDS, 65536, 65536, 0, 0},
    [MKDS_KCL_JUMP] = {SURFACE_JUMP, 65536, 65536, 0, 607406},
    [MKDS_KCL_ROAD_NO_DRIVERS] = {SURFACE_NONE, 0, 0, 0, 0},
    [MKDS_KCL_WALL_NO_DRIVERS] = {SURFACE_NONE, 0, 0, 0, 0},
    [MKDS_KCL_CANNON] = {SURFACE_CANNON, 65536, 65536, 0, CANNON_SPEED}, /* one row per KTPC target */
    [MKDS_KCL_EDGE_WALL] = {SURFACE_WALL, 65536, 65536, 0, 0},
    [MKDS_KCL_FALLS_WATER] = {SURFACE_WATER, 65536, 65536, 0, 0},
    [MKDS_KCL_BOOST_MIN_SPEED] = {SURFACE_BOOST, 65536, 65536, 196608, 0},
    [MKDS_KCL_LOOP] = {SURFACE_ROAD, 65536, 65536, 0, 0},
    [MKDS_KCL_SPECIAL_ROAD] = {SURFACE_ROAD, 65536, 65536, 0, 0},
    [MKDS_KCL_WALL_3] = {SURFACE_WALL, 65536, 65536, 0, 0},
    [MKDS_KCL_RECALCULATE_ROUTE] = {SURFACE_NONE, 0, 0, 0, 0}, /* vertical CPU trigger planes */
};

/* MapObj IDs (MarioKartToolbox MkdsMapObjectId, plus 313: Yoshi Valley
 * scenery it leaves unnamed). Item boxes become ITEM rows;
 * every other known object is scenery, sound or a hazard DS6 converts. */
/* MapObj IDs, names and model files, after MarioKartToolbox's object
 * database (LICENSE-NOTICE.md). Scenery is baked into the course mesh;
 * pending families (hazards, enemies, moving parts, effects) await DS6 and
 * are listed in META mksc:incomplete. */
enum
{
    MAPOBJ_ITEM,    /* ITEM rows */
    MAPOBJ_SILENT,  /* sound emitters, controllers and markers: nothing to draw */
    MAPOBJ_SCENERY, /* static or texture-animated model */
    MAPOBJ_VARIANTS, /* scenery whose NSBTP is an appearance catalogue, not a clock */
    MAPOBJ_SOLID,   /* scenery with a wall prism around it */
    MAPOBJ_PLATFORM, /* moving part: OBJS row, own mesh, motion and owned collision */
    MAPOBJ_HAZARD,   /* OBJS row whose contact sphere hits karts, moving or not */
    MAPOBJ_EFFECT,   /* particle controller: not drawn */
    MAPOBJ_PENDING,
};
typedef struct MapObj
{
    uint16_t id;
    uint8_t family;
    const char *name, *models[2];
    uint8_t solid_percent; /* SOLID: wall radius, percent of the placed model's narrower half width */
    uint8_t motion, speed, lift; /* HAZARD: HAZARD_* motion, metres/second, metres */
    uint8_t reaction;            /* HAZARD: what touching it does (REACT_*) */
} MapObj;
enum
{
    HAZARD_STILL,  /* in place */
    HAZARD_PATROL, /* along its PATH (if any) at `speed`, facing each leg */
    HAZARD_BOB,    /* rises `lift` at `speed`, rests, drops back, rests */
    HAZARD_PULSE,  /* in place, live half of every two seconds */
    HAZARD_SLAM,   /* rises `lift` at `speed`, holds, slams down at eight times that, rests */
    HAZARD_LUNGE,  /* on a chain from its post: lunges `setting1 low` map units at the track, holds, returns */
    HAZARD_FLIP    /* a pinball flipper: swings about its hinge, setting1 low picks the side */
};
/* The reaction the game gives each enemy (mksc:hazard-reaction): Goombas,
 * Piranha Plants and fire spin a kart out; traffic, Chain Chomps, boulders
 * and Bullet Bills tumble it; Thwomps squash it; props bump it. */
enum { REACT_BUMP, REACT_SPIN, REACT_TUMBLE, REACT_SQUASH };
static const char *const reaction_names[] = {"bump", "spin", "tumble", "squash"};
static const MapObj map_objects[] = {
    {1, MAPOBJ_SCENERY, "BeachWater", {"beach_waterA", "beach_waterC"}, 0, 0, 0, 0, 0},
    {2, MAPOBJ_SILENT, "FallsWaterDst", {0}, 0, 0, 0, 0, 0}, /* where falls water pulls karts */
    {3, MAPOBJ_SCENERY, "TownWater", {"town_waterA", "town_waterC"}, 0, 0, 0, 0, 0},
    {5, MAPOBJ_EFFECT, "WaterSplash", {0}, 0, HAZARD_STILL, 0, 0, 0},
    {6, MAPOBJ_SCENERY, "YoshiWater", {"yoshi_waterC"}, 0, 0, 0, 0, 0},
    {8, MAPOBJ_SILENT, "AmbientSfxEmitter", {0}, 0, 0, 0, 0, 0},
    {9, MAPOBJ_SCENERY, "HyudoroWater", {"hyudoro_waterA", "hyudoro_waterC"}, 0, 0, 0, 0, 0},
    {10, MAPOBJ_SILENT, "BirdSfxEmitter", {0}, 0, 0, 0, 0, 0},
    {12, MAPOBJ_SCENERY, "MiniStage3Water", {"mini_stage3_waterA", "mini_stage3_waterC"}, 0, 0, 0, 0, 0},
    {13, MAPOBJ_SCENERY, "Puddle", {"puddle"}, 0, 0, 0, 0, 0},
    {14, MAPOBJ_EFFECT, "LavaSplash", {0}, 0, HAZARD_STILL, 0, 0, 0},
    {15, MAPOBJ_EFFECT, "HyudoroWaterSplash", {0}, 0, HAZARD_STILL, 0, 0, 0},
    {16, MAPOBJ_EFFECT, "MrStageSteam", {0}, 0, HAZARD_STILL, 0, 0, 0},
    {101, MAPOBJ_ITEM, "Itembox", {0}, 0, 0, 0, 0, 0},
    {102, MAPOBJ_SILENT, "DummyPole", {0}, 0, 0, 0, 0, 0},
    {103, MAPOBJ_HAZARD, "Woodbox1", {"woodbox1"}, 0, HAZARD_STILL, 0, 0, REACT_BUMP},
    {104, MAPOBJ_PENDING, "Coin", {0}, 0, 0, 0, 0, 0},
    {105, MAPOBJ_SILENT, "BalloonController", {0}, 0, 0, 0, 0, 0},
    {106, MAPOBJ_PENDING, "Shine", {0}, 0, 0, 0, 0, 0},
    {107, MAPOBJ_SILENT, "ShineController", {0}, 0, 0, 0, 0, 0},
    {108, MAPOBJ_PENDING, "Balloon", {0}, 0, 0, 0, 0, 0},
    {110, MAPOBJ_SILENT, "MissionGate", {0}, 0, 0, 0, 0, 0},
    {111, MAPOBJ_PENDING, "UnkItembox", {0}, 0, 0, 0, 0, 0},
    {112, MAPOBJ_PENDING, "ShineBalloon", {0}, 0, 0, 0, 0, 0},
    {201, MAPOBJ_ITEM, "MoveItembox", {0}, 0, HAZARD_STILL, 0, 0, 0},
    {202, MAPOBJ_PLATFORM, "KoopaBlock", {"koopa_block"}, 0, 0, 0, 0, 0},
    {203, MAPOBJ_PLATFORM, "Gear", {"gear_black"}, 0, 0, 0, 0, 0},
    {204, MAPOBJ_PLATFORM, "Bridge", {"bridge"}, 0, 0, 0, 0, 0},
    {205, MAPOBJ_PLATFORM, "SecondHand", {"second_hand"}, 0, 0, 0, 0, 0},
    {206, MAPOBJ_PLATFORM, "TestCylinder", {"test_cylinder"}, 0, 0, 0, 0, 0},
    {207, MAPOBJ_HAZARD, "Pendulum", {"pendulum"}, 0, 0, 0, 0, REACT_BUMP},
    {208, MAPOBJ_PLATFORM, "RotaryRoom", {"rotary_room"}, 0, 0, 0, 0, 0},
    {209, MAPOBJ_PLATFORM, "RotaryBridge", {"rotary_bridge"}, 0, 0, 0, 0, 0},
    {210, MAPOBJ_SOLID, "Dram", {"dram"}, 90, HAZARD_STILL, 0, 0, 0},
    {301, MAPOBJ_SCENERY, "BeachTree1Nocol", {"BeachTree1"}, 0, 0, 0, 0, 0},
    {302, MAPOBJ_SOLID, "BeachTree1", {"BeachTree1"}, 15, 0, 0, 0, 0},
    {303, MAPOBJ_SOLID, "EarthenPipe", {"earthen_pipe1"}, 95, HAZARD_STILL, 0, 0, 0},
    {304, MAPOBJ_SOLID, "OpaTree1", {"opa_tree1"}, 20, 0, 0, 0, 0},
    {305, MAPOBJ_SOLID, "OlgPipe1", {"OlgPipe1"}, 95, 0, 0, 0, 0},
    {306, MAPOBJ_SOLID, "OlgMush1", {"OlgMush1"}, 35, 0, 0, 0, 0},
    {307, MAPOBJ_SOLID, "Of6Yoshi1", {"of6yoshi1"}, 70, 0, 0, 0, 0},
    {308, MAPOBJ_SOLID, "Cow", {"cow"}, 60, 0, 0, 0, 0},
    {309, MAPOBJ_SOLID, "NsCannon1", {"NsCannon1"}, 70, 0, 0, 0, 0},
    {310, MAPOBJ_SOLID, "MiniDokan", {"mini_dokan"}, 95, 0, 0, 0, 0},
    {312, MAPOBJ_SOLID, "GardenTree1", {"GardenTree1"}, 20, 0, 0, 0, 0},
    {313, MAPOBJ_SCENERY, "Unknown313", {"kamome"}, 0, HAZARD_STILL, 0, 0, 0},
    {314, MAPOBJ_SOLID, "CrossTree1", {"CrossTree1"}, 20, 0, 0, 0, 0},
    {315, MAPOBJ_SCENERY, "Boo", {"teresa"}, 0, HAZARD_STILL, 0, 0, 0},
    {316, MAPOBJ_HAZARD, "Bakubaku", {"bakubaku"}, 0, HAZARD_BOB, 4, 4, REACT_SPIN},
    {317, MAPOBJ_SILENT, "TeresaController", {0}, 0, 0, 0, 0, 0},
    {318, MAPOBJ_SOLID, "BankTree1", {"Bank_Tree1"}, 20, 0, 0, 0, 0},
    {319, MAPOBJ_SCENERY, "GardenTree1Nocol", {"GardenTree1"}, 0, 0, 0, 0, 0},
    {320, MAPOBJ_SCENERY, "Chandelier", {"chandelier"}, 0, 0, 0, 0, 0},
    {321, MAPOBJ_SCENERY, "TownTree1Nocol", {"TownTree1"}, 0, 0, 0, 0, 0},
    {322, MAPOBJ_SOLID, "MarioTree3", {"MarioTree3"}, 20, 0, 0, 0, 0},
    {323, MAPOBJ_SCENERY, "SnowTree1Nocol", {"Snow_Tree1"}, 0, 0, 0, 0, 0},
    {325, MAPOBJ_SOLID, "TownTree1", {"TownTree1"}, 20, 0, 0, 0, 0},
    {326, MAPOBJ_SOLID, "SnowTree1", {"Snow_Tree1"}, 20, 0, 0, 0, 0},
    {327, MAPOBJ_SCENERY, "DeTree1Nocol", {"DeTree1"}, 0, 0, 0, 0, 0},
    {328, MAPOBJ_SOLID, "DeTree1", {"DeTree1"}, 40, 0, 0, 0, 0},
    {329, MAPOBJ_SOLID, "BankEgg1", {"BankEgg1"}, 80, 0, 0, 0, 0},
    {331, MAPOBJ_SOLID, "KinoHouse1", {"KinoHouse1"}, 90, 0, 0, 0, 0},
    {332, MAPOBJ_SOLID, "KinoHouse2", {"KinoHouse2"}, 90, 0, 0, 0, 0},
    {333, MAPOBJ_SOLID, "KinoMount1", {"KinoMount1"}, 90, 0, 0, 0, 0},
    {334, MAPOBJ_SOLID, "KinoMount2", {"KinoMount2"}, 90, 0, 0, 0, 0},
    {335, MAPOBJ_SOLID, "OlaTree1C", {"olaTree1c"}, 20, 0, 0, 0, 0},
    {336, MAPOBJ_SOLID, "OsaTree1C", {"osaTree1c"}, 20, 0, 0, 0, 0},
    {337, MAPOBJ_SCENERY, "Picture1", {"picture1"}, 0, HAZARD_STILL, 0, 0, 0},
    {338, MAPOBJ_SCENERY, "Picture2", {"picture2"}, 0, HAZARD_STILL, 0, 0, 0},
    {339, MAPOBJ_SOLID, "Om6Tree1", {"om6Tree1"}, 20, 0, 0, 0, 0},
    {340, MAPOBJ_SCENERY, "RainStar", {"RainStar"}, 0, HAZARD_STILL, 0, 0, 0},
    {341, MAPOBJ_SCENERY, "Of6TreeNocol", {"Of6Tree1"}, 0, 0, 0, 0, 0},
    {342, MAPOBJ_SOLID, "Of6Tree", {"Of6Tree1"}, 20, 0, 0, 0, 0},
    {343, MAPOBJ_VARIANTS, "Pianta", {"TownMonte"}, 0, HAZARD_STILL, 0, 0, 0},
    {344, MAPOBJ_SCENERY, "Airship", {"airship"}, 0, HAZARD_STILL, 0, 0, 0},
    {345, MAPOBJ_PENDING, "StopSign", {0}, 0, 0, 0, 0, 0},
    {401, MAPOBJ_HAZARD, "Goomba", {"kuribo"}, 0, HAZARD_PATROL, 2, 0, REACT_SPIN},
    {402, MAPOBJ_HAZARD, "Rock", {"rock"}, 0, HAZARD_PATROL, 10, 0, REACT_TUMBLE},
    {403, MAPOBJ_HAZARD, "Thwomp", {"dossun"}, 0, HAZARD_SLAM, 3, 5, REACT_SQUASH},
    {404, MAPOBJ_PENDING, "Stubbed404", {0}, 0, 0, 0, 0, 0},
    {405, MAPOBJ_HAZARD, "Bus", {"bus_a"}, 0, HAZARD_PATROL, 8, 0, REACT_TUMBLE},
    {406, MAPOBJ_HAZARD, "Chain Chomp", {"wanwan", "wanwan_kui"}, 0, HAZARD_LUNGE, 25, 0, REACT_TUMBLE},
    {407, MAPOBJ_SILENT, "Chain Chomp's Chain", {0}, 0, 0, 0, 0, 0}, /* drawn by each Chain Chomp */
    {408, MAPOBJ_HAZARD, "MkdEfBubble", {"mkd_ef_bubble"}, 0, HAZARD_BOB, 6, 4, REACT_SPIN},
    {409, MAPOBJ_HAZARD, "Choropu", {"choropu"}, 0, HAZARD_BOB, 2, 1, REACT_SPIN},
    {410, MAPOBJ_HAZARD, "Car", {"car_a"}, 0, HAZARD_PATROL, 9, 0, REACT_TUMBLE},
    {411, MAPOBJ_HAZARD, "Cheep-Cheep", {"pukupuku"}, 0, HAZARD_BOB, 4, 3, REACT_SPIN},
    {412, MAPOBJ_HAZARD, "Truck", {"truck_a"}, 0, HAZARD_PATROL, 7, 0, REACT_TUMBLE},
    {413, MAPOBJ_HAZARD, "Snowman", {"sman_bottom"}, 0, HAZARD_STILL, 0, 0, REACT_SPIN},
    {414, MAPOBJ_SCENERY, "Kanoke64", {"kanoke_64"}, 0, HAZARD_STILL, 0, 0, 0},
    {415, MAPOBJ_HAZARD, "Swoop", {"basabasa"}, 0, HAZARD_STILL, 0, 0, REACT_SPIN},
    {416, MAPOBJ_SCENERY, "Swoop Spawner", {"basabasa"}, 0, HAZARD_STILL, 0, 0, 0},
    {417, MAPOBJ_HAZARD, "Cannon (Moving)", {"NsKiller1"}, 0, HAZARD_PATROL, 3, 0, REACT_TUMBLE},
    {418, MAPOBJ_HAZARD, "Bullet Bill", {"NsKiller2"}, 0, HAZARD_PATROL, 15, 0, REACT_TUMBLE},
    {419, MAPOBJ_HAZARD, "Move Tree", {"move_tree"}, 0, HAZARD_STILL, 0, 0, REACT_BUMP},
    {420, MAPOBJ_HAZARD, "MkdEfBurner", {"mkd_ef_burner"}, 0, HAZARD_PULSE, 0, 0, REACT_SPIN},
    {421, MAPOBJ_HAZARD, "Chain Chomp (Moving)", {"wanwan"}, 0, HAZARD_PATROL, 3, 0, REACT_TUMBLE},
    {422, MAPOBJ_HAZARD, "Piranha Plant (2D)", {"ob_pakkun_sf"}, 0, HAZARD_STILL, 0, 0, REACT_SPIN},
    {423, MAPOBJ_HAZARD, "Rocky Wrench", {"poo"}, 0, HAZARD_BOB, 2, 1, REACT_SPIN},
    {424, MAPOBJ_HAZARD, "Bound", {"bound"}, 0, HAZARD_STILL, 0, 0, REACT_BUMP},
    {425, MAPOBJ_HAZARD, "Flipper", {"flipper"}, 0, HAZARD_FLIP, 0, 0, REACT_BUMP},
    {426, MAPOBJ_HAZARD, "Piranha Plant", {"PakkunBody", "PakkunZHead"}, 0, HAZARD_STILL, 0, 0, REACT_SPIN},
    {427, MAPOBJ_SILENT, "PakkunFire", {0}, 0, 0, 0, 0, 0}, /* thrown by each Piranha Plant */
    {428, MAPOBJ_HAZARD, "Crab", {"crab"}, 0, HAZARD_PATROL, 2, 0, REACT_SPIN},
    {429, MAPOBJ_SCENERY, "Sun", {"sun"}, 0, HAZARD_STILL, 0, 0, 0},
    {430, MAPOBJ_SILENT, "SunFireSnake", {0}, 0, 0, 0, 0, 0}, /* thrown by the sun */
    {431, MAPOBJ_HAZARD, "Fireball2", {"fireball2"}, 0, HAZARD_STILL, 0, 0, REACT_SPIN},
    {432, MAPOBJ_HAZARD, "Iron Ball", {"IronBall"}, 0, HAZARD_PATROL, 8, 0, REACT_TUMBLE},
    {433, MAPOBJ_HAZARD, "Rock2", {"rock2"}, 0, HAZARD_PATROL, 8, 0, REACT_TUMBLE},
    {434, MAPOBJ_HAZARD, "Pokey", {"sanbo_b", "sanbo_h"}, 0, HAZARD_PATROL, 1, 0, REACT_SPIN},
    {435, MAPOBJ_SCENERY, "Iron Ball (No Collision)", {"IronBall"}, 0, HAZARD_STILL, 0, 0, 0},
    {436, MAPOBJ_SOLID, "Cream", {"cream"}, 80, HAZARD_STILL, 0, 0, 0},
    {437, MAPOBJ_SOLID, "Berry", {"berry"}, 80, HAZARD_STILL, 0, 0, 0},
    {438, MAPOBJ_EFFECT, "Unk438MrParticle", {0}, 0, HAZARD_STILL, 0, 0, 0},
    {501, MAPOBJ_PENDING, "Big Bully", {0}, 0, 0, 0, 0, 0},
    {502, MAPOBJ_PENDING, "King Chill Bully", {0}, 0, 0, 0, 0, 0},
    {503, MAPOBJ_PENDING, "King Goomba", {0}, 0, 0, 0, 0, 0},
    {504, MAPOBJ_PENDING, "King Bob-omb", {0}, 0, 0, 0, 0, 0},
    {507, MAPOBJ_PENDING, "Eyerok", {0}, 0, 0, 0, 0, 0},
    {508, MAPOBJ_PENDING, "Wiggler", {0}, 0, 0, 0, 0, 0},
    {509, MAPOBJ_PENDING, "King Boo", {0}, 0, 0, 0, 0, 0},
    {510, MAPOBJ_PENDING, "Wiggler's Body Part", {0}, 0, 0, 0, 0, 0},
    {511, MAPOBJ_PENDING, "Unk511MrParticle", {0}, 0, 0, 0, 0, 0},
    {512, MAPOBJ_SILENT, "IwanteController", {0}, 0, 0, 0, 0, 0},
};

static const MapObj *map_object(Course *c, const MkdsObject *object, size_t index)
{
    for (size_t known = 0; known < sizeof(map_objects) / sizeof(map_objects[0]); ++known)
        if (map_objects[known].id == object->id)
            return &map_objects[known];
    ri_fail(c->m.ctx, "%s: course_map.nkm object %zu has unknown MapObj ID %u", c->m.where, index, (unsigned)object->id);
    return NULL;
}

#define GATE_MARGIN (1 << 16)
/* DS checkpoints are 2D lines of unbounded height; only successor gates
 * count, so tall gates keep a misjudged floor from hiding one. */
#define GATE_HALF_HEIGHT (256 << 16)
#define RESPAWN_RADIUS (2 << 16)
#define ITEM_RADIUS (3 << 15)
#define ITEM_RESPAWN_TICKS 120u
#define GRID_SLOTS 8u
#define FLOOR_NORMAL 1024 /* fx16 up component above which a triangle is floor */

typedef struct Gameplay
{
    const MkdsKcl *kcl;
    const MkdsMap *map;
    uint32_t *gate_checkpoints; /* gate -> CPOI index */
    uint32_t *gate_stages, *stage_first; /* gates of stage s: stage_first[s]..[s+1] */
    uint32_t gate_count, stage_count;
    int32_t (*gate_centers)[3]; /* engine Q16 */
} Gameplay;

/* Height of the floor under DS (x, z) nearest to `near` and, when surface
 * is not NULL, its SURFACE_* type; 0 when none. */
static int floor_at(const MkdsKcl *kcl, int32_t x, int32_t z, int32_t near, int32_t *height, uint8_t *surface)
{
    int found = 0;
    int64_t best = 0;
    for (size_t index = 0; index < kcl->count; ++index)
    {
        const MkdsKclTriangle *t = &kcl->triangles[index];
        if (t->normal[1] <= FLOOR_NORMAL)
            continue;
        const uint32_t type = MKDS_KCL_TYPE(t->attribute);
        if (type >= MKDS_KCL_TYPE_COUNT || kcl_surfaces[type].type == SURFACE_NONE ||
            kcl_surfaces[type].type == SURFACE_WALL)
            continue;
        int64_t sign = 0;
        int inside = 1;
        for (unsigned k = 0; k < 3 && inside; ++k)
        {
            const int32_t *a = t->vertex[k], *b = t->vertex[(k + 1) % 3];
            const int64_t edge = ((int64_t)b[0] - a[0]) * ((int64_t)z - a[2]) - ((int64_t)b[2] - a[2]) * ((int64_t)x - a[0]);
            if (edge != 0)
            {
                if (sign != 0 && (edge > 0) != (sign > 0))
                    inside = 0;
                sign = edge;
            }
        }
        if (!inside)
            continue;
        /* Plane through vertex 0 with the face normal: n.(p - v0) = 0. */
        const int32_t *v = t->vertex[0];
        const int64_t y = v[1] - (t->normal[0] * ((int64_t)x - v[0]) + t->normal[2] * ((int64_t)z - v[2])) / t->normal[1];
        const int64_t distance = y > near ? y - near : near - y;
        if (!found || distance < best)
        {
            found = 1;
            best = distance;
            *height = (int32_t)y;
            if (surface)
                *surface = kcl_surfaces[type].type;
        }
    }
    return found;
}

static void engine_point(Course *c, const int32_t ds[3], int32_t out[3])
{
    out[0] = mkds_units(&c->m, ds[0]);
    out[1] = mkds_units(&c->m, ds[2]);
    out[2] = mkds_units(&c->m, ds[1]);
}

/* Yaw quaternion (x, y, z, w) for a DS rotation of `degrees` (fx32) about +Y,
 * which faces (sin r, 0, cos r); the reflection makes that engine yaw
 * half a turn minus r. */
static void yaw_quaternion(int32_t degrees, int32_t quaternion[4], uint16_t *yaw_out)
{
    const uint16_t yaw = (uint16_t)(32768 - mkds_round_div((int64_t)degrees * 65536, 360 * 4096));
    int32_t sine, cosine;
    portable_fixed_3d_sincos((uint16_t)(yaw / 2), &sine, &cosine);
    quaternion[0] = quaternion[1] = 0;
    quaternion[2] = sine;
    quaternion[3] = cosine;
    if (yaw_out)
        *yaw_out = yaw;
}

/* Cannon trigger SURF row for KCL variant `variant`: it fires karts at the
 * KTPC point with that ID (or in that place when unnumbered), leaving on the
 * point's heading. */
static uint32_t cannon_surface(Course *c, const Gameplay *g, unsigned variant)
{
    const MkdsPose *pose = NULL;
    for (size_t index = 0; index < g->map->cannon_count && !pose; ++index)
        if (g->map->cannons[index].index == (int32_t)variant)
            pose = &g->map->cannons[index];
    if (!pose && variant < g->map->cannon_count && g->map->cannons[variant].index == -1)
        pose = &g->map->cannons[variant]; /* unnumbered points go by order */
    if (!pose)
        ri_fail(c->m.ctx, "%s: cannon variant %u has no KTPC target", c->m.where, variant);
    int32_t target[3], heading[3], sine, cosine;
    engine_point(c, pose->position, target);
    portable_fixed_3d_sincos((uint16_t)mkds_round_div((int64_t)pose->rotation[1] * 65536, 360 * 4096), &sine, &cosine);
    /* DS heading (sin r, 0, cos r) through engine_point's axis swap. */
    heading[0] = mkds_round_div(sine, 1 << 14);
    heading[1] = mkds_round_div(cosine, 1 << 14);
    heading[2] = 0;
    const uint32_t first = mkds_property_vector(&c->m, "mksc:cannon-target", target);
    mkds_property_vector(&c->m, "mksc:cannon-heading", heading);
    const uint32_t row[12] = {SURFACE_CANNON, 0, 65536, 65536, 0, 0, 0, CANNON_SPEED, 65536, 0, first, 2};
    mkds_words(&c->m, MKDS_T_SURF, row, 12);
    return c->m.tables[MKDS_T_SURF].count - 1;
}

/* The SURF row of a KCL type (per variant for cannons), added on first use. */
typedef struct Surfaces
{
    uint32_t of[MKDS_KCL_TYPE_COUNT][MKDS_FLOOR_MATERIALS + 1u][4], cannon_of[8]; /* by type, floor and KCL light */
} Surfaces;

/* DS floor material (CSfxFloorMat, mkds_sound.h) of each KCL type and
 * variant, from the ARM9 table at 0x215a6fc; NO_FLOOR has no floor sound.
 * Courses only override wall types, which have no floor sound. */
enum { ASPH, CONC, EDGE, DIRT, STON, SAND, ICE, GRAS, WATR, SNOW, BORD, CARP, MESH, PLAS, IRON, RAIN, MUD, STAI,
       NO_FLOOR = MKDS_FLOOR_MATERIALS };
static const uint8_t kcl_floor[MKDS_KCL_TYPE_COUNT][8] = {
    [MKDS_KCL_ROAD] = {ASPH, DIRT, STON, CONC, BORD, SNOW, MESH, IRON},
    [MKDS_KCL_SLIPPERY] = {SAND, DIRT, GRAS, WATR, SNOW, NO_FLOOR, NO_FLOOR, NO_FLOOR},
    [MKDS_KCL_WEAK_OFF_ROAD] = {SAND, WATR, SNOW, DIRT, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR},
    [MKDS_KCL_OFF_ROAD] = {DIRT, MUD, GRAS, SAND, CARP, SNOW, NO_FLOOR, NO_FLOOR},
    [MKDS_KCL_SOUND_TRIGGER] = {NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR},
    [MKDS_KCL_HEAVY_OFF_ROAD] = {DIRT, MUD, GRAS, SAND, GRAS, NO_FLOOR, NO_FLOOR, NO_FLOOR},
    [MKDS_KCL_SLIPPERY_2] = {ICE, MUD, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR},
    [MKDS_KCL_BOOST] = {PLAS, PLAS, PLAS, DIRT, PLAS, NO_FLOOR, NO_FLOOR, NO_FLOOR},
    [MKDS_KCL_WALL] = {NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR},
    [MKDS_KCL_INVISIBLE_WALL] = {NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR},
    [MKDS_KCL_OUT_OF_BOUNDS] = {DIRT, CONC, STON, SNOW, WATR, NO_FLOOR, NO_FLOOR, NO_FLOOR},
    [MKDS_KCL_FALL] = {NO_FLOOR, WATR, MUD, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR},
    [MKDS_KCL_JUMP] = {PLAS, PLAS, PLAS, DIRT, PLAS, NO_FLOOR, NO_FLOOR, NO_FLOOR},
    [MKDS_KCL_ROAD_NO_DRIVERS] = {NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR},
    [MKDS_KCL_WALL_NO_DRIVERS] = {NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR},
    [MKDS_KCL_CANNON] = {NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR},
    [MKDS_KCL_EDGE_WALL] = {NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR},
    [MKDS_KCL_FALLS_WATER] = {WATR, WATR, WATR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR},
    [MKDS_KCL_BOOST_MIN_SPEED] = {PLAS, PLAS, PLAS, DIRT, PLAS, NO_FLOOR, NO_FLOOR, NO_FLOOR},
    [MKDS_KCL_LOOP] = {RAIN, RAIN, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR},
    [MKDS_KCL_SPECIAL_ROAD] = {GRAS, CARP, RAIN, GRAS, STAI, SAND, DIRT, NO_FLOOR},
    [MKDS_KCL_WALL_3] = {NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR},
    [MKDS_KCL_RECALCULATE_ROUTE] = {NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR, NO_FLOOR},
};

static uint32_t kcl_surface(Course *c, Gameplay *g, Surfaces *surfaces, uint32_t type, uint32_t attribute)
{
    if (type == MKDS_KCL_CANNON)
    {
        const unsigned variant = (attribute >> 5) & 7u;
        if (surfaces->cannon_of[variant] == MKDS_ABSENT)
            surfaces->cannon_of[variant] = cannon_surface(c, g, variant);
        return surfaces->cannon_of[variant];
    }
    const uint32_t floor = kcl_floor[type][MKDS_KCL_VARIANT(attribute)];
    /* Attribute bits 0-1 pick the STAG light colour of karts on the prism. */
    const uint32_t light = attribute & 3u;
    if (surfaces->of[type][floor][light] == MKDS_ABSENT)
    {
        /* KCL is one-sided: karts pass walls from behind, as in DS; loop
         * road holds karts to its face. The floor material picks its slip,
         * landing and driving sounds (SURF mksc:sound-surface). */
        const int wall = kcl_surfaces[type].type == SURFACE_WALL, loop = type == MKDS_KCL_LOOP;
        const uint32_t property = c->m.tables[MKDS_T_PROP].count;
        uint32_t properties = 0;
        if (wall || loop)
        {
            mkds_property(&c->m, wall ? "mksc:one-sided" : "mksc:surface-gravity", MKDS_PROPERTY_BOOLEAN, 1, 0);
            ++properties;
        }
        if (floor != NO_FLOOR)
        {
            mkds_property(&c->m, "mksc:sound-surface", MKDS_PROPERTY_INTEGER, floor + 1u, 0);
            ++properties;
        }
        if (floor == WATR)
        {
            mkds_property(&c->m, "mksc:splash", MKDS_PROPERTY_BOOLEAN, 1, 0);
            ++properties;
        }
        if (light)
        {
            mkds_property(&c->m, "mksc:light", MKDS_PROPERTY_INTEGER, light, 0);
            ++properties;
        }
        surfaces->of[type][floor][light] = c->m.tables[MKDS_T_SURF].count;
        const uint32_t row[12] = {kcl_surfaces[type].type, 0, kcl_surfaces[type].friction,
                                  kcl_surfaces[type].speed_ratio, 0, wall ? 13107u : 0u,
                                  kcl_surfaces[type].boost, kcl_surfaces[type].launch, 65536, 0,
                                  properties ? property : 0u, properties};
        mkds_words(&c->m, MKDS_T_SURF, row, 12);
    }
    return surfaces->of[type][floor][light];
}

/* One COLT row over engine-space corners whose winding faces the surface
 * side; floors respawn on the stretch nearest their centroid. */
static void collision_triangle(Course *c, Gameplay *g, const int32_t corners[3][3], uint32_t surface, int wall,
                               uint32_t owner)
{
    int64_t center[3] = {0, 0, 0};
    for (unsigned k = 0; k < 3; ++k)
        for (unsigned axis = 0; axis < 3; ++axis)
            center[axis] += corners[k][axis];
    uint32_t row[8] = {collision_vertex(c, corners[0]), collision_vertex(c, corners[1]),
                       collision_vertex(c, corners[2]), surface, 0, MKDS_ABSENT, MKDS_ABSENT, owner};
    if (!wall && g->gate_count)
    {
        /* Respawn row of the stretch nearest the centroid: from gate i to i+1. */
        int64_t best = INT64_MAX;
        for (uint32_t gate = 0; gate < g->gate_count; ++gate)
        {
            const uint32_t next = (g->gate_stages[gate] + 1) % g->stage_count;
            for (uint32_t successor = g->stage_first[next]; successor < g->stage_first[next + 1]; ++successor)
            {
                const int32_t *a = g->gate_centers[gate], *b = g->gate_centers[successor];
                int64_t ab[3], ap[3], length = 0, along = 0;
                for (unsigned axis = 0; axis < 3; ++axis)
                {
                    ab[axis] = ((int64_t)b[axis] - a[axis]) >> 8;
                    ap[axis] = (center[axis] / 3 - a[axis]) >> 8;
                    length += ab[axis] * ab[axis];
                    along += ab[axis] * ap[axis];
                }
                int64_t distance = 0;
                for (unsigned axis = 0; axis < 3; ++axis)
                {
                    int64_t offset = ap[axis];
                    if (length > 0 && along > 0)
                        offset -= along >= length ? ab[axis] : ab[axis] * along / length;
                    distance += offset * offset;
                }
                if (distance < best)
                {
                    best = distance;
                    row[5] = gate;
                }
            }
        }
    }
    mkds_words(&c->m, MKDS_T_COLT, row, 8);
}

/* Collision: one SURF row per KCL type used (per variant for cannons), COLT
 * rows in KCL order, then the platform MapObjs' faces as road or wall. */
static void collision(Course *c, Gameplay *g)
{
    Surfaces surfaces;
    for (unsigned type = 0; type < MKDS_KCL_TYPE_COUNT; ++type)
        for (unsigned floor = 0; floor <= MKDS_FLOOR_MATERIALS; ++floor)
            for (unsigned light = 0; light < 4; ++light)
                surfaces.of[type][floor][light] = MKDS_ABSENT;
    for (unsigned variant = 0; variant < 8; ++variant)
        surfaces.cannon_of[variant] = MKDS_ABSENT;
    for (size_t index = 0; index < g->kcl->count; ++index)
    {
        const MkdsKclTriangle *t = &g->kcl->triangles[index];
        const uint32_t type = MKDS_KCL_TYPE(t->attribute);
        if (type >= MKDS_KCL_TYPE_COUNT)
            ri_fail(c->m.ctx, "%s: course_collision.kcl prism %zu has unknown collision type %u (attribute 0x%04x)",
                    c->m.where, index, (unsigned)type, (unsigned)t->attribute);
        if (kcl_surfaces[type].type == SURFACE_NONE || (t->attribute & COLLISION_IGNORES_DRIVERS))
            continue;
        const uint32_t surface = kcl_surface(c, g, &surfaces, type, t->attribute);
        int32_t corners[3][3];
        /* The axis reflection reverses winding: keep the face normal up. */
        engine_point(c, t->vertex[0], corners[0]);
        engine_point(c, t->vertex[2], corners[1]);
        engine_point(c, t->vertex[1], corners[2]);
        collision_triangle(c, g, (const int32_t (*)[3])corners, surface, kcl_surfaces[type].type == SURFACE_WALL, MKDS_ABSENT);
    }
    /* Moving MapObj faces, already wound outward in engine space, owned by
     * their OBJS row. All are road: a face turns from floor to wall as the
     * object moves, so the kart's own slope test sorts them at each pose. */
    const uint8_t *faces = c->platform_faces->data;
    for (uint32_t face = 0; face < c->platform_face_count; ++face)
    {
        int32_t corners[3][3];
        for (unsigned k = 0; k < 9; ++k)
            corners[k / 3][k % 3] = (int32_t)ri_le32(faces + face * 40 + k * 4);
        int64_t ab[3], ac[3];
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            ab[axis] = (int64_t)corners[1][axis] - corners[0][axis];
            ac[axis] = (int64_t)corners[2][axis] - corners[0][axis];
        }
        const int64_t cross[3] = {ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2],
                                  ab[0] * ac[1] - ab[1] * ac[0]};
        int32_t normal[3];
        if (!portable_fixed_3d_normalize(cross, normal))
            continue;
        collision_triangle(c, g, (const int32_t (*)[3])corners, kcl_surface(c, g, &surfaces, MKDS_KCL_ROAD, 0), 0,
                           ri_le32(faces + face * 40 + 36));
    }
    const uint8_t *walls = c->solid_walls->data;
    for (uint32_t face = 0; face < c->solid_wall_count; ++face)
    {
        int32_t corners[3][3];
        for (unsigned k = 0; k < 9; ++k)
            corners[k / 3][k % 3] = (int32_t)ri_le32(walls + face * 36 + k * 4);
        collision_triangle(c, g, (const int32_t (*)[3])corners, kcl_surface(c, g, &surfaces, MKDS_KCL_WALL, 0), 1, MKDS_ABSENT);
    }
    if (c->m.tables[MKDS_T_COLT].count == 0)
        ri_fail(c->m.ctx, "%s: course_collision.kcl has no drivable collision", c->m.where);
}

/* Lap gates from the CPAT section graph, one stage per checkpoint. Paths
 * that run in parallel (a group whose paths share predecessors and successors)
 * become one stage holding each path's first checkpoint. */
static void gates(Course *c, Gameplay *g)
{
    const MkdsMap *map = g->map;
    const size_t paths = map->checkpoint_path_count;
    if (map->checkpoint_count == 0 || paths == 0 || map->checkpoint_paths[0].start != 0)
        ri_fail(c->m.ctx, "%s: course_map.nkm checkpoints do not start at the finish line", c->m.where);
    RiCtx *ctx = c->m.ctx;
    g->gate_checkpoints = (uint32_t *)(void *)ri_buf_zero(ctx, map->checkpoint_count * sizeof(uint32_t))->data;
    g->gate_stages = (uint32_t *)(void *)ri_buf_zero(ctx, map->checkpoint_count * sizeof(uint32_t))->data;
    g->stage_first = (uint32_t *)(void *)ri_buf_zero(ctx, (map->checkpoint_count + 1) * sizeof(uint32_t))->data;
    g->gate_centers = (int32_t(*)[3])(void *)ri_buf_zero(ctx, map->checkpoint_count * sizeof(int32_t[3]))->data;
    uint8_t *placed = ri_buf_zero(ctx, paths)->data;
    int32_t group[3] = {0, -1, -1};
    size_t group_size = 1, covered = 0;
    do
    {
        for (size_t k = 0; k < group_size; ++k)
        {
            const MkdsPath *p = &map->checkpoint_paths[group[k]];
            if (placed[group[k]]++)
                ri_fail(ctx, "%s: course_map.nkm checkpoint path %d repeats in a lap", c->m.where, (int)group[k]);
            ++covered;
            if (memcmp(p->next, map->checkpoint_paths[group[0]].next, sizeof(p->next)) != 0)
                ri_fail(ctx, "%s: course_map.nkm parallel checkpoint paths %d and %d lead apart", c->m.where,
                        (int)group[0], (int)group[k]);
            const uint32_t count = group_size == 1 ? p->length : 1;
            for (uint32_t index = 0; index < count; ++index)
            {
                if (group_size == 1 || k == 0)
                    g->stage_first[g->stage_count++] = g->gate_count;
                g->gate_stages[g->gate_count] = g->stage_count - 1;
                g->gate_checkpoints[g->gate_count++] = p->start + index;
            }
        }
        const int32_t last_index = group[0];
        const MkdsPath *last = &map->checkpoint_paths[last_index];
        group_size = 0;
        for (unsigned n = 0; n < 3; ++n)
            if (last->next[n] >= 0)
                group[group_size++] = last->next[n];
        if (group_size == 0)
            ri_fail(ctx, "%s: course_map.nkm checkpoint path %d does not loop", c->m.where, (int)last_index);
    } while (!(group_size == 1 && group[0] == 0));
    g->stage_first[g->stage_count] = g->gate_count;
    if (covered != paths)
        ri_fail(ctx, "%s: course_map.nkm leaves %zu checkpoint paths off the lap", c->m.where, paths - covered);

    for (uint32_t gate = 0; gate < g->gate_count; ++gate)
    {
        const MkdsCheckpoint *cp = &map->checkpoints[g->gate_checkpoints[gate]];
        const int64_t dx = (int64_t)cp->right[0] - cp->left[0], dz = (int64_t)cp->right[1] - cp->left[1];
        int32_t right[3], normal[3];
        const int64_t across[3] = {dx, dz, 0}, ahead[3] = {dz, -dx, 0};
        if (!portable_fixed_3d_normalize(across, right) || !portable_fixed_3d_normalize(ahead, normal))
            ri_fail(c->m.ctx, "%s: course_map.nkm checkpoint %u has no width", c->m.where, (unsigned)g->gate_checkpoints[gate]);
        /* Height: the floor under the midpoint nearest the closest CPU point. */
        const int32_t mid[2] = {(int32_t)(((int64_t)cp->left[0] + cp->right[0]) / 2),
                                (int32_t)(((int64_t)cp->left[1] + cp->right[1]) / 2)};
        int32_t near = 0, height;
        int64_t best = INT64_MAX;
        for (size_t point = 0; point < map->enemy_point_count; ++point)
        {
            const int32_t *p = map->enemy_points[point].position;
            const int64_t ex = ((int64_t)p[0] - mid[0]) >> 12, ez = ((int64_t)p[2] - mid[1]) >> 12;
            if (ex * ex + ez * ez < best)
            {
                best = ex * ex + ez * ez;
                near = p[1];
            }
        }
        if (!floor_at(g->kcl, mid[0], mid[1], near, &height, NULL))
            height = near;
        const int32_t center_ds[3] = {mid[0], height, mid[1]};
        engine_point(c, center_ds, g->gate_centers[gate]);
        const int64_t width = (dx * right[0] + dz * right[1]) >> 30;
        const uint32_t half_width = (uint32_t)mkds_units(&c->m, width / 2) + GATE_MARGIN;
        const uint32_t row[16] = {(uint32_t)g->gate_centers[gate][0], (uint32_t)g->gate_centers[gate][1],
                                  (uint32_t)g->gate_centers[gate][2],
                                  (uint32_t)normal[0], (uint32_t)normal[1], 0, (uint32_t)right[0], (uint32_t)right[1], 0,
                                  half_width, GATE_HALF_HEIGHT, g->gate_stages[gate], g->gate_stages[gate],
                                  c->m.tables[MKDS_T_CPED].count, g->stage_first[(g->gate_stages[gate] + 1) % g->stage_count + 1] -
                                      g->stage_first[(g->gate_stages[gate] + 1) % g->stage_count],
                                  gate == 0 ? 5u : 4u};
        mkds_words(&c->m, MKDS_T_CPNT, row, 16);
        const uint32_t next = (g->gate_stages[gate] + 1) % g->stage_count;
        for (uint32_t successor = g->stage_first[next]; successor < g->stage_first[next + 1]; ++successor)
            mkds_words(&c->m, MKDS_T_CPED, &successor, 1);
    }
}

/* Successor lists of NKM points as compressed rows: each point leads to the
 * next in its path and a path's last point to the paths it links (CPU and
 * item routes). Battle MEPA links name points and run both ways, so battle
 * graphs also keep every link reversed, listed after all forward links so a
 * point's first successor continues its patrol forward. */
typedef struct PointGraph
{
    uint32_t *first, *targets; /* targets[first[p]..first[p + 1]] */
} PointGraph;

static size_t point_links(const MkdsPath *paths, size_t path_count, int battle, uint32_t (*out)[2])
{
    size_t count = 0;
    for (int reverse = 0; reverse < (battle ? 2 : 1); ++reverse)
        for (size_t path = 0; path < path_count; ++path)
        {
            const MkdsPath *p = &paths[path];
            const uint32_t last = p->start + p->length - 1;
            for (uint32_t point = p->start; point < last; ++point, ++count)
                if (out)
                    out[count][0] = reverse ? point + 1 : point, out[count][1] = reverse ? point : point + 1;
            for (unsigned n = 0; n < MKDS_PATH_LINKS; ++n)
                for (int way = 0; way < (battle ? 2 : 1); ++way)
                {
                    const int32_t link = way ? p->previous[n] : p->next[n];
                    if (link < 0)
                        continue;
                    const uint32_t from = way ? p->start : last, to = battle ? (uint32_t)link : paths[link].start;
                    if (out)
                        out[count][0] = reverse != way ? to : from, out[count][1] = reverse != way ? from : to;
                    ++count;
                }
        }
    return count;
}

static PointGraph point_graph(RiCtx *ctx, const MkdsPath *paths, size_t path_count, size_t point_count, int battle)
{
    const size_t link_count = point_links(paths, path_count, battle, NULL);
    uint32_t(*links)[2] = (uint32_t(*)[2])(void *)ri_buf_zero(ctx, (link_count + 1) * sizeof(*links))->data;
    point_links(paths, path_count, battle, links);
    PointGraph graph;
    graph.first = (uint32_t *)(void *)ri_buf_zero(ctx, (point_count + 1) * sizeof(uint32_t))->data;
    graph.targets = (uint32_t *)(void *)ri_buf_zero(ctx, (link_count + 1) * sizeof(uint32_t))->data;
    for (size_t index = 0; index < link_count; ++index)
        graph.first[links[index][0] + 1]++;
    for (size_t point = 0; point < point_count; ++point)
        graph.first[point + 1] += graph.first[point];
    uint32_t *fill = (uint32_t *)(void *)ri_buf_copy(ctx, graph.first, point_count * sizeof(uint32_t))->data;
    for (size_t index = 0; index < link_count; ++index)
    {
        /* Duplicate links (a both-ways pair named twice) keep one edge. */
        const uint32_t from = links[index][0], to = links[index][1];
        int seen = 0;
        for (uint32_t k = graph.first[from]; k < fill[from]; ++k)
            seen |= graph.targets[k] == to;
        if (!seen)
            graph.targets[fill[from]++] = to;
    }
    /* Close the gaps duplicates left. */
    uint32_t write = 0;
    for (size_t point = 0; point < point_count; ++point)
    {
        const uint32_t begin = graph.first[point], end = fill[point];
        graph.first[point] = write;
        for (uint32_t k = begin; k < end; ++k)
            graph.targets[write++] = graph.targets[k];
    }
    graph.first[point_count] = write;
    return graph;
}

/* A kart dropped at a respawn starts from rest. Where the CPU line ahead
 * of it reaches a jump, a fall or a gap within RUN_UP metres (jump chains
 * over lava, ramps over pits), the respawn moves back along that line to the
 * nearest point with RUN_UP metres of ordinary floor ahead, so recovery
 * reaches the jump at speed; it never moves behind the finish line or more
 * than RUN_UP_SEARCH metres. The row keeps its gate: gates the kart
 * crosses again before its own are not successors and never count. */
#define RUN_UP 40
#define RUN_UP_SEARCH 160
#define MAP_METRE (MKDS_UNITS_PER_METRE * 4096)

/* Whether a moving MapObj's face at rest (a gear, a bridge) lies under
 * engine (x, y) within 4 m of height z. */
static int platform_under(const Course *c, const int32_t at[3])
{
    const uint8_t *faces = c->platform_faces->data;
    for (uint32_t face = 0; face < c->platform_face_count; ++face)
    {
        int32_t v[3][3];
        for (unsigned k = 0; k < 9; ++k)
            v[k / 3][k % 3] = (int32_t)ri_le32(faces + face * 40 + k * 4);
        int64_t sign = 0;
        int inside = 1;
        for (unsigned k = 0; k < 3 && inside; ++k)
        {
            const int32_t *a = v[k], *b = v[(k + 1) % 3];
            const int64_t edge = ((int64_t)b[0] - a[0]) * ((int64_t)at[1] - a[1]) -
                                 ((int64_t)b[1] - a[1]) * ((int64_t)at[0] - a[0]);
            if (edge != 0)
            {
                if (sign != 0 && (edge > 0) != (sign > 0))
                    inside = 0;
                sign = edge;
            }
        }
        const int64_t height = ((int64_t)v[0][2] + v[1][2] + v[2][2]) / 3 - at[2];
        if (inside && height < (4 << 16) && height > -(4 << 16))
            return 1;
    }
    return 0;
}

/* Ordinary floor under DS (x, z) near height `near`: not a jump, fall or
 * gap (cannons launch at any speed; moving platforms bridge gaps). */
static int runway_at(Course *c, const MkdsKcl *kcl, int32_t x, int32_t z, int32_t near, int32_t *height)
{
    uint8_t surface = 0;
    const int found = floor_at(kcl, x, z, near, height, &surface);
    if (found && surface != SURFACE_OUT_OF_BOUNDS && *height > near - 2 * MAP_METRE && *height < near + 2 * MAP_METRE)
        return surface != SURFACE_JUMP;
    const int32_t ds[3] = {x, near, z};
    int32_t at[3];
    engine_point(c, ds, at);
    if (platform_under(c, at))
    {
        *height = near;
        return 1;
    }
    return found && surface != SURFACE_JUMP && surface != SURFACE_OUT_OF_BOUNDS;
}

/* The CPU line as a walk: the point `offset` map units past EPOI `from`
 * toward `to`. */
typedef struct LineWalk
{
    uint32_t from, to;
    int64_t offset, length;
} LineWalk;

typedef struct Line
{
    const MkdsMap *map;
    PointGraph graph;
    uint32_t *previous; /* a predecessor per point (its path's, else any) */
    uint8_t *linked;    /* 0: no predecessor, 1: another path's, 2: its path's */
} Line;

static int64_t span(const int32_t a[3], const int32_t b[3])
{
    uint64_t squared = 0;
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        const int64_t d = ((int64_t)b[axis] - a[axis]) >> 4;
        squared += (uint64_t)(d * d);
    }
    return (int64_t)portable_fixed_3d_sqrt(squared) << 4;
}

static void walk_at(const Line *line, const LineWalk *w, int32_t out[3])
{
    const int32_t *a = line->map->enemy_points[w->from].position, *b = line->map->enemy_points[w->to].position;
    for (unsigned axis = 0; axis < 3; ++axis)
        out[axis] = w->length ? (int32_t)(a[axis] + ((int64_t)b[axis] - a[axis]) * w->offset / w->length) : a[axis];
}

/* Moves the walk `step` map units along the line (negative: back); 0 where
 * the line ends. */
static int walk_step(const Line *line, LineWalk *w, int64_t step)
{
    const MkdsPoint *points = line->map->enemy_points;
    w->offset += step;
    for (unsigned guard = 0; w->offset >= w->length; ++guard)
    {
        if (guard == 64 || line->graph.first[w->to] == line->graph.first[w->to + 1])
            return 0;
        w->offset -= w->length;
        w->from = w->to;
        w->to = line->graph.targets[line->graph.first[w->from]];
        w->length = span(points[w->from].position, points[w->to].position);
    }
    for (unsigned guard = 0; w->offset < 0; ++guard)
    {
        if (guard == 64 || !line->linked[w->from])
            return 0;
        w->to = w->from;
        w->from = line->previous[w->from];
        w->length = span(points[w->from].position, points[w->to].position);
        w->offset += w->length;
    }
    return 1;
}

/* Whether 2D segments pq and ab (x, z) cross. */
static int crosses(const int32_t p[3], const int32_t q[3], const int32_t a[2], const int32_t b[2])
{
    const int64_t d1 = ((int64_t)q[0] - p[0]) * ((int64_t)a[1] - p[2]) - ((int64_t)q[2] - p[2]) * ((int64_t)a[0] - p[0]);
    const int64_t d2 = ((int64_t)q[0] - p[0]) * ((int64_t)b[1] - p[2]) - ((int64_t)q[2] - p[2]) * ((int64_t)b[0] - p[0]);
    const int64_t d3 = ((int64_t)b[0] - a[0]) * ((int64_t)p[2] - a[1]) - ((int64_t)b[1] - a[1]) * ((int64_t)p[0] - a[0]);
    const int64_t d4 = ((int64_t)b[0] - a[0]) * ((int64_t)q[2] - a[1]) - ((int64_t)b[1] - a[1]) * ((int64_t)q[0] - a[0]);
    return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0));
}

/* The DS turn (65536 per revolution) facing (sin, cos) along (dx, dz). */
static uint16_t heading(int64_t dx, int64_t dz)
{
    uint16_t best = 0;
    int64_t best_dot = INT64_MIN;
    for (uint32_t step = 0; step < 65536; step += 256)
    {
        int32_t sine, cosine;
        portable_fixed_3d_sincos((uint16_t)step, &sine, &cosine);
        const int64_t dot = (dx >> 8) * (sine >> 10) + (dz >> 8) * (cosine >> 10);
        if (dot > best_dot)
            best_dot = dot, best = (uint16_t)step;
    }
    for (int32_t delta = 128; delta >= 1; delta /= 2)
        for (int sign = -1; sign <= 1; sign += 2)
        {
            const uint16_t at = (uint16_t)(best + sign * delta);
            int32_t sine, cosine;
            portable_fixed_3d_sincos(at, &sine, &cosine);
            const int64_t dot = (dx >> 8) * (sine >> 10) + (dz >> 8) * (cosine >> 10);
            if (dot > best_dot)
                best_dot = dot, best = at;
        }
    return best;
}

/* A run-up start for a respawn at `ds` (DS map units): 1 with its floor
 * position and DS heading when the respawn must move, else 0. */
static int run_up(Course *c, const Line *line, const Gameplay *g, const int32_t ds[3], int32_t out[3], uint16_t *turn)
{
    const MkdsMap *map = line->map;
    /* The nearest stretch of the line, in 3D so stacked roads stay apart. */
    LineWalk w = {0, 0, 0, 0};
    int64_t best = INT64_MAX;
    for (uint32_t a = 0; a < map->enemy_point_count; ++a)
        for (uint32_t k = line->graph.first[a]; k < line->graph.first[a + 1]; ++k)
        {
            const uint32_t b = line->graph.targets[k];
            const int32_t *pa = map->enemy_points[a].position, *pb = map->enemy_points[b].position;
            const int64_t length = span(pa, pb);
            int64_t along = 0;
            for (unsigned axis = 0; axis < 3; ++axis)
                along += (((int64_t)pb[axis] - pa[axis]) >> 6) * (((int64_t)ds[axis] - pa[axis]) >> 6);
            along = length ? (along << 12) / length : 0;
            along = along < 0 ? 0 : along > length ? length : along;
            LineWalk candidate = {a, b, along, length};
            int32_t at[3];
            walk_at(line, &candidate, at);
            const int64_t distance = span(at, ds);
            if (distance < best)
                best = distance, w = candidate;
        }
    if (best == INT64_MAX || best > 16 * MAP_METRE)
        return 0;
    /* Ordinary floor ahead of the respawn. */
    int32_t at[3], height;
    LineWalk ahead = w;
    int clear = 0;
    for (; clear < RUN_UP; ++clear)
    {
        if (!walk_step(line, &ahead, MAP_METRE))
            return 0;
        walk_at(line, &ahead, at);
        if (!runway_at(c, g->kcl, at[0], at[2], at[1], &height))
            break;
    }
    if (clear >= RUN_UP)
        return 0;
    const MkdsCheckpoint *finish = &map->checkpoints[g->gate_checkpoints[0]];
    int32_t before[3];
    walk_at(line, &w, before);
    for (int back = 0; back < RUN_UP_SEARCH; ++back)
    {
        if (!walk_step(line, &w, -MAP_METRE))
            return 0;
        walk_at(line, &w, at);
        if (crosses(before, at, finish->left, finish->right))
            return 0;
        memcpy(before, at, sizeof(before));
        if (!runway_at(c, g->kcl, at[0], at[2], at[1], &height))
            clear = 0;
        else if (++clear >= RUN_UP)
        {
            const int32_t *a = map->enemy_points[w.from].position, *b = map->enemy_points[w.to].position;
            out[0] = at[0];
            out[1] = height;
            out[2] = at[2];
            *turn = heading((int64_t)b[0] - a[0], (int64_t)b[2] - a[2]);
            return 1;
        }
    }
    return 0;
}

static Line cpu_line(RiCtx *ctx, const MkdsMap *map)
{
    Line line = {map, point_graph(ctx, map->enemy_paths, map->enemy_path_count, map->enemy_point_count, 0), NULL,
                 NULL};
    line.previous = (uint32_t *)(void *)ri_buf_zero(ctx, (map->enemy_point_count + 1) * sizeof(uint32_t))->data;
    line.linked = ri_buf_zero(ctx, map->enemy_point_count + 1)->data;
    for (uint32_t a = 0; a < map->enemy_point_count; ++a)
        for (uint32_t k = line.graph.first[a]; k < line.graph.first[a + 1]; ++k)
        {
            const uint32_t b = line.graph.targets[k];
            if (line.linked[b] < 2)
            {
                line.previous[b] = a;
                line.linked[b] = a + 1 == b ? 2 : 1;
            }
        }
    return line;
}

/* One respawn per gate at its checkpoint's KTPJ pose, or its run-up start;
 * collision under each stretch names it. Recovering CPUs and route aids
 * reacquire `cpu_route`. */
static void respawns(Course *c, Gameplay *g, uint32_t cpu_route)
{
    const Line line = cpu_line(c->m.ctx, g->map);
    for (uint32_t gate = 0; gate < g->gate_count; ++gate)
    {
        const MkdsPose *pose = &g->map->respawns[g->map->checkpoints[g->gate_checkpoints[gate]].respawn];
        int32_t position[3], quaternion[4], start[3];
        uint16_t turn;
        if (run_up(c, &line, g, pose->position, start, &turn))
        {
            engine_point(c, start, position);
            yaw_quaternion((int32_t)mkds_round_div((int64_t)turn * 360 * 4096, 65536), quaternion, NULL);
        }
        else
        {
            engine_point(c, pose->position, position);
            yaw_quaternion(pose->rotation[1], quaternion, NULL);
        }
        const uint32_t row[12] = {(uint32_t)position[0], (uint32_t)position[1], (uint32_t)position[2],
                                  (uint32_t)quaternion[0], (uint32_t)quaternion[1], (uint32_t)quaternion[2],
                                  (uint32_t)quaternion[3], gate, RESPAWN_RADIUS, 0, cpu_route, 0};
        mkds_words(&c->m, MKDS_T_RESP, row, 12);
    }
}

/* A closed route over the NKM points reachable from point 0. Points only
 * respawns enter are left out. */
static void route(Course *c, uint32_t kind, const MkdsPoint *points, size_t point_count, const MkdsPath *paths,
                  size_t path_count, int battle, const char *tag)
{
    if (point_count == 0 || path_count == 0 || paths[0].start != 0)
        ri_fail(c->m.ctx, "%s: course_map.nkm %s route is empty", c->m.where, tag);
    RiCtx *ctx = c->m.ctx;
    const PointGraph graph = point_graph(ctx, paths, path_count, point_count, battle);
    uint32_t *node_of = (uint32_t *)(void *)ri_buf_zero(ctx, point_count * sizeof(uint32_t))->data;
    uint32_t *queue = (uint32_t *)(void *)ri_buf_zero(ctx, point_count * sizeof(uint32_t))->data;
    uint8_t *reached = ri_buf_zero(ctx, point_count)->data;
    size_t head = 0, tail = 1;
    reached[0] = 1;
    while (head < tail)
    {
        const uint32_t point = queue[head++];
        for (uint32_t k = graph.first[point]; k < graph.first[point + 1]; ++k)
            if (!reached[graph.targets[k]])
            {
                reached[graph.targets[k]] = 1;
                queue[tail++] = graph.targets[k];
            }
    }
    const uint32_t first = c->m.tables[MKDS_T_RTND].count;
    uint32_t nodes = 0;
    for (uint32_t point = 0; point < point_count; ++point)
        if (reached[point])
            node_of[point] = first + nodes++;
    /* Route aids drive CPU routes with the same controller as the rivals. */
    const uint32_t property = kind == 0 ? mkds_property(&c->m, "mksc:route-pilot-continuous", MKDS_PROPERTY_BOOLEAN, 1, 0)
                                        : 0;
    const uint32_t header[8] = {kind, first, nodes, first, 1, property, kind == 0 ? 1u : 0u, 0};
    mkds_words(&c->m, MKDS_T_RTHD, header, 8);
    for (uint32_t point = 0; point < point_count; ++point)
    {
        if (!reached[point])
            continue;
        const uint32_t edge_first = c->m.tables[MKDS_T_RTED].count, edges = graph.first[point + 1] - graph.first[point];
        if (edges == 0)
            ri_fail(ctx, "%s: course_map.nkm %s point %u ends without a successor", c->m.where, tag, (unsigned)point);
        for (uint32_t k = graph.first[point]; k < graph.first[point + 1]; ++k)
            mkds_words(&c->m, MKDS_T_RTED, &node_of[graph.targets[k]], 1);
        int32_t position[3];
        engine_point(c, points[point].position, position);
        const int32_t radius = points[point].radius > 0 ? mkds_units(&c->m, points[point].radius) : (2 << 16);
        const uint32_t row[8] = {(uint32_t)position[0], (uint32_t)position[1], (uint32_t)position[2],
                                 (uint32_t)radius, 0, edge_first, edges, 0};
        mkds_words(&c->m, MKDS_T_RTND, row, 8);
    }
}

/* Battle spawns: one GRID slot per KTPS pose, on the floor below it. */
static void spawns(Course *c, Gameplay *g)
{
    if (g->map->start_count < 4 || g->map->start_count > GRID_SLOTS)
        ri_fail(c->m.ctx, "%s: course_map.nkm has %u battle start positions (KTPS), not 4..%u", c->m.where,
                (unsigned)g->map->start_count, (unsigned)GRID_SLOTS);
    for (size_t slot = 0; slot < g->map->start_count; ++slot)
    {
        const MkdsPose *start = &g->map->starts[slot];
        int32_t quaternion[4], ds[3] = {start->position[0], start->position[1], start->position[2]}, height;
        uint16_t yaw;
        yaw_quaternion(start->rotation[1], quaternion, &yaw);
        if (floor_at(g->kcl, ds[0], ds[2], ds[1], &height, NULL))
            ds[1] = height;
        int32_t position[3];
        engine_point(c, ds, position);
        const uint32_t row[8] = {(uint32_t)position[0], (uint32_t)position[1], (uint32_t)position[2],
                                 (uint32_t)quaternion[0], (uint32_t)quaternion[1], (uint32_t)quaternion[2],
                                 (uint32_t)quaternion[3], 0};
        mkds_words(&c->m, MKDS_T_GRID, row, 8);
    }
}

/* Start grid: KTPS pose, then two staggered columns behind it. */
static void grid(Course *c, Gameplay *g)
{
    if (g->map->start_count == 0)
        ri_fail(c->m.ctx, "%s: course_map.nkm has no start position (KTPS)", c->m.where);
    const MkdsPose *start = &g->map->starts[0];
    int32_t quaternion[4], sine, cosine;
    uint16_t yaw;
    yaw_quaternion(start->rotation[1], quaternion, &yaw);
    portable_fixed_3d_sincos(yaw, &sine, &cosine);
    /* Engine forward is (sin, -cos, 0) and right (cos, sin, 0); slots step
     * 3 m back and alternate 2.5 m either side, in map units (20 per metre). */
    for (uint32_t slot = 0; slot < GRID_SLOTS; ++slot)
    {
        const int64_t back = 60 * 4096 * (int64_t)slot / 2 + 20 * 4096, side = (slot % 2 ? 50 : -50) * 4096;
        const int64_t ex = (-back * sine + side * cosine) >> 30, ey = (back * cosine + side * sine) >> 30;
        /* Engine (x, y) is DS (x, z). */
        int32_t ds[3] = {(int32_t)(start->position[0] + ex), start->position[1], (int32_t)(start->position[2] + ey)};
        int32_t height;
        if (floor_at(g->kcl, ds[0], ds[2], ds[1], &height, NULL))
            ds[1] = height;
        int32_t position[3];
        engine_point(c, ds, position);
        const uint32_t row[8] = {(uint32_t)position[0], (uint32_t)position[1], (uint32_t)position[2],
                                 (uint32_t)quaternion[0], (uint32_t)quaternion[1], (uint32_t)quaternion[2],
                                 (uint32_t)quaternion[3], 0};
        mkds_words(&c->m, MKDS_T_GRID, row, 8);
    }
}

/* Item boxes become ITEM rows. */
static void objects(Course *c, Gameplay *g)
{
    for (size_t index = 0; index < g->map->object_count; ++index)
    {
        const MkdsObject *object = &g->map->objects[index];
        if (map_object(c, object, index)->family != MAPOBJ_ITEM)
            continue;
        int32_t position[3], quaternion[4];
        uint16_t yaw;
        engine_point(c, object->position, position);
        yaw_quaternion(object->rotation[1], quaternion, &yaw);
        const uint32_t row[8] = {(uint32_t)position[0], (uint32_t)position[1], (uint32_t)position[2], ITEM_RADIUS,
                                 yaw, ITEM_RESPAWN_TICKS, 0, 0};
        mkds_words(&c->m, MKDS_T_ITEM, row, 8);
    }
}

static void gameplay(Course *c, const MkdsKcl *kcl, const MkdsMap *map)
{
    Gameplay g;
    memset(&g, 0, sizeof(g));
    g.kcl = kcl;
    g.map = map;
    if (c->source->battle)
    {
        /* Arenas: CPUs patrol the MEPO graph between fights; spawns and item boxes as placed. */
        collision(c, &g);
        route(c, 0, map->enemy_points, map->enemy_point_count, map->enemy_paths, map->enemy_path_count, 1, "CPU (MEPO)");
        spawns(c, &g);
        objects(c, &g);
        return;
    }
    gates(c, &g);
    collision(c, &g);
    const uint32_t cpu_route = c->m.tables[MKDS_T_RTHD].count;
    route(c, 0, map->enemy_points, map->enemy_point_count, map->enemy_paths, map->enemy_path_count, 0, "CPU (EPOI)");
    respawns(c, &g, cpu_route);
    route(c, 1, map->item_points, map->item_point_count, map->item_paths, map->item_path_count, 0, "item (IPOI)");
    grid(c, &g);
    objects(c, &g);
}

/* The top-screen intro: the CAME chain from the camera marked first, each a
 * CAMR row whose kind-2 route the camera travels over its duration while
 * its aim and field of view ease from the first values to the second. */
static void intro(Course *c, const MkdsMap *map)
{
    int32_t camera_index = -1;
    for (size_t index = 0; index < map->camera_count && camera_index < 0; ++index)
        if (map->cameras[index].intro == 1)
            camera_index = (int32_t)index;
    for (uint32_t order = 0; camera_index >= 0; ++order)
    {
        const MkdsCamera *camera = &map->cameras[camera_index];
        if (order >= map->camera_count)
            ri_fail(c->m.ctx, "%s: course_map.nkm intro cameras loop", c->m.where);
        uint32_t fov[2];
        for (unsigned k = 0; k < 2; ++k)
        {
            if (camera->fov[k] == 0 || camera->fov[k] >= 90)
                ri_fail(c->m.ctx, "%s: course_map.nkm CAME %d has field of view %u", c->m.where, (int)camera_index,
                        (unsigned)camera->fov[k]);
            fov[k] = (uint32_t)mkds_round_div((int64_t)camera->fov[k] * 2 * 65536, 360);
        }
        if (camera->duration == 0)
            ri_fail(c->m.ctx, "%s: course_map.nkm intro CAME %d has no duration", c->m.where, (int)camera_index);
        int32_t position[3], target[2][3];
        engine_point(c, camera->position, position);
        engine_point(c, camera->target[0], target[0]);
        engine_point(c, camera->target[1], target[1]);
        uint32_t route = MKDS_ABSENT;
        if (camera->rail >= 0)
        {
            const MkdsRail *rail = &map->rails[camera->rail];
            route = c->m.tables[MKDS_T_RTHD].count;
            const uint32_t first = c->m.tables[MKDS_T_RTND].count;
            const uint32_t header[8] = {2, first, rail->length, first, 0, 0, 0, 0};
            mkds_words(&c->m, MKDS_T_RTHD, header, 8);
            for (uint32_t point = 0; point < rail->length; ++point)
            {
                int32_t at[3];
                engine_point(c, map->rail_points[rail->start + point], at);
                const uint32_t next = first + point + 1, last = point + 1 == rail->length;
                const uint32_t row[8] = {(uint32_t)at[0], (uint32_t)at[1], (uint32_t)at[2], 2u << 16, 0,
                                         c->m.tables[MKDS_T_RTED].count, last ? 0u : 1u, 0};
                mkds_words(&c->m, MKDS_T_RTND, row, 8);
                if (!last)
                    mkds_words(&c->m, MKDS_T_RTED, &next, 1);
            }
        }
        const uint32_t properties = mkds_property(&c->m, "mksc:intro-order", MKDS_PROPERTY_INTEGER, order, 0);
        mkds_property(&c->m, "mksc:camera-ticks", MKDS_PROPERTY_INTEGER, camera->duration, 0);
        mkds_property(&c->m, "mksc:camera-fov-end", MKDS_PROPERTY_INTEGER, fov[1], 0);
        mkds_property_vector(&c->m, "mksc:camera-target-begin", target[0]);
        mkds_property_vector(&c->m, "mksc:camera-target-end", target[1]);
        const uint32_t row[16] = {(uint32_t)position[0], (uint32_t)position[1], (uint32_t)position[2], 0, 0, 0,
                                  1u << 30, fov[0], 1u << 14, 2048u << 16, route, MKDS_ABSENT, 0, 0, properties, 5};
        mkds_words(&c->m, MKDS_T_CAMR, row, 16);
        camera_index = camera->next;
    }
}

/* An OBJI pose: per-axis scale and rotation Y, X then Z (Q12), map-unit position. */
static void placement(const MkdsObject *object, MkdsPlacement *place)
{
    int32_t sine[3], cosine[3];
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        portable_fixed_3d_sincos((uint16_t)mkds_round_div((int64_t)object->rotation[axis] * 65536, 360 * 4096),
                                 &sine[axis], &cosine[axis]);
        sine[axis] = mkds_round_div(sine[axis], 1 << 18);
        cosine[axis] = mkds_round_div(cosine[axis], 1 << 18);
    }
    const int64_t ry[9] = {cosine[1], 0, sine[1], 0, 4096, 0, -sine[1], 0, cosine[1]};
    const int64_t rx[9] = {4096, 0, 0, 0, cosine[0], -sine[0], 0, sine[0], cosine[0]};
    const int64_t rz[9] = {cosine[2], -sine[2], 0, sine[2], cosine[2], 0, 0, 0, 4096};
    int64_t yx[9];
    for (unsigned row = 0; row < 3; ++row)
        for (unsigned col = 0; col < 3; ++col)
            yx[row * 3 + col] =
                mkds_round_div(ry[row * 3] * rx[col] + ry[row * 3 + 1] * rx[3 + col] + ry[row * 3 + 2] * rx[6 + col], 4096);
    for (unsigned row = 0; row < 3; ++row)
        for (unsigned col = 0; col < 3; ++col)
            place->r[row * 3 + col] =
                mkds_round_div(yx[row * 3] * rz[col] + yx[row * 3 + 1] * rz[3 + col] + yx[row * 3 + 2] * rz[6 + col], 4096);
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        place->scale[axis] = object->scale[axis];
        place->translate[axis] = object->position[axis];
    }
}

/* Engine axes swap DS y and z: the rotation conjugated by that swap. */
static const unsigned engine_axis[3] = {0, 2, 1};

/* Unit quaternion (x, y, z, w; Q30) of a Q12 rotation matrix, by the
 * largest of the trace and diagonal (Shepperd). */
static void matrix_quaternion(const int64_t m[9], int32_t q[4])
{
    const int64_t one = (int64_t)1 << 30;
    int64_t r[9];
    for (unsigned i = 0; i < 9; ++i)
        r[i] = m[i] << 18;
    const int64_t trace = r[0] + r[4] + r[8];
    int64_t v[4];
    if (trace > 0)
    {
        const int64_t s = (int64_t)portable_fixed_3d_sqrt((uint64_t)(one + trace) << 30) * 2; /* 4w */
        v[0] = (r[7] - r[5]) * one / s;
        v[1] = (r[2] - r[6]) * one / s;
        v[2] = (r[3] - r[1]) * one / s;
        v[3] = s / 4;
    }
    else
    {
        const unsigned i = r[0] >= r[4] && r[0] >= r[8] ? 0 : r[4] >= r[8] ? 1 : 2, j = (i + 1) % 3, k = (i + 2) % 3;
        const int64_t s =
            (int64_t)portable_fixed_3d_sqrt((uint64_t)(one + r[i * 4] - r[j * 4] - r[k * 4]) << 30) * 2; /* 4 q_i */
        v[i] = s / 4;
        v[j] = (r[j * 3 + i] + r[i * 3 + j]) * one / s;
        v[k] = (r[k * 3 + i] + r[i * 3 + k]) * one / s;
        v[3] = (r[k * 3 + j] - r[j * 3 + k]) * one / s;
    }
    uint64_t length = 0;
    for (unsigned n = 0; n < 4; ++n)
        length += (uint64_t)((v[n] >> 8) * (v[n] >> 8));
    const int64_t norm = (int64_t)portable_fixed_3d_sqrt(length) << 8;
    for (unsigned n = 0; n < 4; ++n)
        q[n] = (int32_t)(norm ? v[n] * one / norm : (n == 3 ? one : 0));
}

/* A MapObj model's NSBCA bone animation, stem.nsbca beside its model (the
 * course archive, then the texture archive); 0 when it has none. */
static int object_animation(Course *c, const NdsFs *archive, const NdsFs *texture_archive, const char *stem,
                            NitroJointAnimation *out)
{
    char path[64], name[160];
    RiData file;
    snprintf(path, sizeof(path), "%s.nsbca", stem);
    const NdsFs *found = nds_find(archive, path, &file) ? archive
                         : texture_archive && nds_find(texture_archive, path, &file) ? texture_archive
                                                                                     : NULL;
    if (!found)
        return 0;
    snprintf(name, sizeof(name), "%s/%s", found->name, path);
    *out = nitro_bca(c->m.ctx, file, (const char *)ri_buf_copy(c->m.ctx, name, strlen(name) + 1)->data);
    return 1;
}

/* A moving or animated MapObj model's own kind-96 mesh (mkds_object_mesh). */
typedef MkdsObjectMesh ObjectMesh;

static ObjectMesh object_mesh(Course *c, const MkdsResources *model, const char *name,
                              const NitroJointAnimation *animation)
{
    char where[96];
    snprintf(where, sizeof(where), "%s MapObj %s", c->m.where, name);
    const uint32_t slot = c->object_mesh_count++;
    if (slot == 256)
        ri_fail(c->m.ctx, "%s: more than 256 moving MapObj models", c->m.where);
    return mkds_object_mesh(c->m.ctx, where, model, animation, MKDS_OBJECT_MESH_ID(c->source->index, slot),
                            MKDS_OBJECT_MATERIAL_ID(c->source->index, slot, 0), 0);
}

/* DS radians/8192 per frame as turns<<32 per tick (2^18/pi per unit). */
static int32_t turn_rate(uint32_t setting)
{
    return mkds_round_div((int64_t)setting * 834430268, 10000);
}

/* An RTHD closed route through `count` engine points; leg i runs at
 * speeds[i] metres/second (Q16). Returns the route row. */
static uint32_t object_route(Course *c, const int32_t (*points)[3], const int64_t *speeds, uint32_t count)
{
    const uint32_t route = c->m.tables[MKDS_T_RTHD].count, first = c->m.tables[MKDS_T_RTND].count;
    const uint32_t header[8] = {2, first, count, first, 1 /* closed loop */, 0, 0, 0};
    mkds_words(&c->m, MKDS_T_RTHD, header, 8);
    for (uint32_t point = 0; point < count; ++point)
    {
        const uint32_t next = first + (point + 1) % count;
        const uint32_t row[8] = {(uint32_t)points[point][0], (uint32_t)points[point][1], (uint32_t)points[point][2],
                                 2u << 16, (uint32_t)(speeds[point] > 0 ? speeds[point] : 1),
                                 c->m.tables[MKDS_T_RTED].count, 1, 0};
        mkds_words(&c->m, MKDS_T_RTND, row, 8);
        mkds_words(&c->m, MKDS_T_RTED, &next, 1);
    }
    return route;
}

/* An object's PATH as a closed route: a looped PATH as is, an open one
 * there and back; at most 32 points, evenly sampled. Speed is `speed`
 * metres/second, or (0) each leg takes six frames per POIT duration unit. */
static uint32_t path_route(Course *c, const MapObj *entry, const MkdsObject *object, const MkdsMap *map,
                           uint32_t speed)
{
    if (object->route < 0 || (size_t)object->route >= map->rail_count)
        ri_fail(c->m.ctx, "%s: %s has no PATH", c->m.where, entry->name);
    const MkdsRail *rail = &map->rails[object->route];
    if (rail->length < 2)
        ri_fail(c->m.ctx, "%s: %s PATH %d has one point", c->m.where, entry->name, (int)object->route);
    uint32_t order[64], count = 0, start = 0;
    /* Patrols (traffic, pinballs) are placed anywhere along a shared PATH.
     * They start from the POIT nearest their placement, so the first leg
     * keeps the placed heading and the route is not shifted by PATH[0]. */
    if (entry->motion == HAZARD_PATROL)
    {
        int64_t nearest = INT64_MAX;
        for (uint32_t k = 0; k < rail->length; ++k)
        {
            int64_t distance = 0;
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                const int64_t delta = ((int64_t)map->rail_points[rail->start + k][axis] - object->position[axis]) / 16;
                distance += delta * delta;
            }
            if (distance < nearest)
            {
                nearest = distance;
                start = k;
            }
        }
    }
    if (entry->motion == HAZARD_PATROL && !rail->loop)
    {
        /* Both endpoints and the starting point: at most 17 distinct points
         * make a there-and-back cycle of at most 32 legs. */
        const uint32_t sampled = rail->length > 16 ? 16 : rail->length;
        for (uint32_t k = 0; k < sampled; ++k)
            order[count++] = k * (rail->length - 1) / (sampled - 1);
        uint32_t at = 0;
        while (order[at] < start) ++at;
        if (order[at] != start)
        {
            memmove(order + at + 1, order + at, (count - at) * sizeof *order);
            order[at] = start;
            ++count;
        }
        const uint32_t forward = count;
        for (uint32_t back = forward - 1; back-- > 1;)
            order[count++] = order[back];
        uint32_t rotated[32];
        for (uint32_t k = 0; k < count; ++k)
            rotated[k] = order[(at + k) % count];
        memcpy(order, rotated, count * sizeof *order);
    }
    else
    {
        const uint32_t sampled = rail->length > 32 ? 32 : rail->length;
        for (uint32_t k = 0; k < sampled; ++k)
            order[count++] = (start + k * rail->length / sampled) % rail->length;
        if (!rail->loop)
            for (uint32_t back = sampled - 1; back-- > 1;)
                order[count++] = order[back];
        while (count > 32)
        {
            uint32_t kept = 0;
            for (uint32_t k = 0; k < count; k += 2)
                order[kept++] = order[k];
            count = kept;
        }
    }
    int32_t points[32][3];
    int64_t speeds[32];
    for (uint32_t k = 0; k < count; ++k)
        engine_point(c, map->rail_points[rail->start + order[k]], points[k]);
    /* Route offsets are relative to point 0, so a patrol leaves exactly
     * from its placement: model and contact sphere both ride the PATH. */
    if (entry->motion == HAZARD_PATROL)
        engine_point(c, object->position, points[0]);
    for (uint32_t k = 0; k < count; ++k)
    {
        if (speed)
        {
            speeds[k] = (int64_t)speed << 16;
            continue;
        }
        const int32_t *at = points[k], *to = points[(k + 1) % count];
        int64_t squared = 0;
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            const int64_t d = ((int64_t)to[axis] - at[axis]) >> 4;
            squared += d * d;
        }
        const int64_t distance = (int64_t)portable_fixed_3d_sqrt((uint64_t)squared) << 4;
        const int64_t duration = map->rail_durations[rail->start + order[k]];
        /* metres/second over 6*duration ticks of 4389/262144 s */
        speeds[k] = distance * 262144 / (6 * (duration > 0 ? duration : 1) * 4389);
    }
    return object_route(c, (const int32_t (*)[3])points, speeds, count);
}

/* A Chain Chomp's post (its placement), its resting spot a quarter of its
 * chain out and its lunge at the full chain (setting1 low, map units),
 * aimed level at the nearest CPU point. */
static void lunge_points(Course *c, const MkdsObject *object, const MkdsMap *map, int32_t post[3], int32_t rest[3],
                         int32_t lunge[3])
{
    engine_point(c, object->position, post);
    int64_t nearest = INT64_MAX;
    int32_t target[3] = {post[0] + 65536, post[1], post[2]};
    for (size_t point = 0; point < map->enemy_point_count; ++point)
    {
        int32_t at[3];
        engine_point(c, map->enemy_points[point].position, at);
        const int64_t dx = ((int64_t)at[0] - post[0]) >> 4, dy = ((int64_t)at[1] - post[1]) >> 4;
        if (dx * dx + dy * dy < nearest)
        {
            nearest = dx * dx + dy * dy;
            memcpy(target, at, sizeof target);
        }
    }
    const int64_t d[3] = {(int64_t)target[0] - post[0], (int64_t)target[1] - post[1], 0};
    int32_t unit[3];
    if (!portable_fixed_3d_normalize(d, unit))
        ri_fail(c->m.ctx, "%s: Chain Chomp sits on its CPU point", c->m.where);
    const uint32_t chain = object->settings[0] & 0xFFFFu;
    const int64_t reach = mkds_units(&c->m, (int32_t)(chain << 12)) - (int64_t)mkds_units(&c->m, 0);
    if (reach <= 0 || reach > 64 * 65536)
        ri_fail(c->m.ctx, "%s: Chain Chomp chain %u out of range", c->m.where, (unsigned)chain);
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        rest[axis] = post[axis] + (int32_t)((reach / 4 * unit[axis]) >> 30);
        lunge[axis] = post[axis] + (int32_t)((reach * unit[axis]) >> 30);
    }
}

/* Out and back between rest and lunge: the lunge at `speed` metres/second,
 * the return at a fifth of it, holding a second at each end. `share`
 * (Q16) scales both legs, so a chain link a share of the way out keeps time. */
static uint32_t lunge_route(Course *c, const int32_t rest[3], const int32_t lunge[3], uint32_t speed, int64_t share)
{
    const int32_t points[2][3] = {{rest[0], rest[1], rest[2]}, {lunge[0], lunge[1], lunge[2]}};
    const int64_t speeds[2] = {((int64_t)speed << 16) * share >> 16, ((int64_t)speed << 16) / 5 * share >> 16};
    const uint32_t route = object_route(c, points, speeds, 2);
    mkds_property(&c->m, "mksc:motion", MKDS_PROPERTY_STRING, mkds_string(&c->m, "route"), 0);
    mkds_property(&c->m, "mksc:route-wait", MKDS_PROPERTY_INTEGER, 60, 0);
    return route;
}

/* A hazard's motion properties (see HAZARD_*); returns their count. */
static uint32_t hazard_motion(Course *c, const MapObj *entry, const MkdsObject *object, const int64_t r[9],
                              const MkdsMap *map, uint32_t *route)
{
    switch (entry->motion)
    {
    case HAZARD_PATROL:
        if (object->route < 0)
            return 0; /* parked */
        *route = path_route(c, entry, object, map, entry->speed);
        mkds_property(&c->m, "mksc:motion", MKDS_PROPERTY_STRING, mkds_string(&c->m, "route"), 0);
        mkds_property(&c->m, "mksc:route-wait", MKDS_PROPERTY_INTEGER, 0, 0);
        mkds_property(&c->m, "mksc:route-face", MKDS_PROPERTY_BOOLEAN, 1, 0);
        return 3;
    case HAZARD_BOB:
    {
        int32_t points[2][3];
        engine_point(c, object->position, points[0]);
        memcpy(points[1], points[0], sizeof points[1]);
        points[1][2] += (int32_t)entry->lift << 16;
        const int64_t speeds[2] = {(int64_t)entry->speed << 16, (int64_t)entry->speed << 17};
        *route = object_route(c, (const int32_t (*)[3])points, speeds, 2);
        mkds_property(&c->m, "mksc:motion", MKDS_PROPERTY_STRING, mkds_string(&c->m, "route"), 0);
        mkds_property(&c->m, "mksc:route-wait", MKDS_PROPERTY_INTEGER, 60, 0);
        return 2;
    }
    case HAZARD_SLAM:
    {
        /* Rest on the floor, rise slowly, hold, slam down at eight times the rise. */
        int32_t points[2][3];
        engine_point(c, object->position, points[0]);
        memcpy(points[1], points[0], sizeof points[1]);
        points[1][2] += (int32_t)entry->lift << 16;
        const int64_t speeds[2] = {(int64_t)entry->speed << 16, (int64_t)entry->speed << 19};
        *route = object_route(c, (const int32_t (*)[3])points, speeds, 2);
        mkds_property(&c->m, "mksc:motion", MKDS_PROPERTY_STRING, mkds_string(&c->m, "route"), 0);
        mkds_property(&c->m, "mksc:route-wait", MKDS_PROPERTY_INTEGER, 60, 0);
        return 2;
    }
    case HAZARD_LUNGE:
    {
        if (!c->chained)
            ri_fail(c->m.ctx, "%s: %s is placed without its post", c->m.where, entry->name);
        int32_t post[3], rest[3], lunge[3];
        lunge_points(c, c->chained, map, post, rest, lunge);
        *route = lunge_route(c, rest, lunge, entry->speed, 65536);
        return 2;
    }
    case HAZARD_FLIP:
    {
        /* About the hinge's up axis, 40 degrees each way, once every 1.5 seconds;
         * the right flipper (setting1 low 0) swings the other way. */
        const int32_t sign = (object->settings[0] & 0xFFFFu) ? 1 : -1;
        const int32_t vector[3] = {sign * (int32_t)r[1] * 16, sign * (int32_t)r[7] * 16, sign * (int32_t)r[4] * 16};
        mkds_property(&c->m, "mksc:motion", MKDS_PROPERTY_STRING, mkds_string(&c->m, "swing"), 0);
        mkds_property_vector(&c->m, "mksc:motion-axis", vector);
        mkds_property(&c->m, "mksc:swing-amplitude", MKDS_PROPERTY_INTEGER, 7282, 0);
        mkds_property(&c->m, "mksc:swing-period", MKDS_PROPERTY_INTEGER, 90, 0);
        return 4;
    }
    case HAZARD_PULSE:
        mkds_property(&c->m, "mksc:hazard-period", MKDS_PROPERTY_INTEGER, 120, 0);
        mkds_property(&c->m, "mksc:hazard-active", MKDS_PROPERTY_INTEGER, 60, 0);
        return 2;
    default:
        return 0;
    }
}

/* The motion of a moving MapObj (after mkjs's objects), as OBJS properties;
 * the reflection onto engine axes reverses every DS turning sense. Returns
 * the property count; *route gets the RTHD row a route motion travels. */
static uint32_t motion(Course *c, const MapObj *entry, const MkdsObject *object, const int64_t r[9],
                       const MkdsMap *map, uint32_t *route)
{
    const uint32_t s1lo = object->settings[0] & 0xFFFFu, s1hi = object->settings[0] >> 16;
    const uint32_t s2lo = object->settings[1] & 0xFFFFu, s2hi = object->settings[1] >> 16;
    *route = MKDS_ABSENT;
    unsigned axis = 1; /* local axis the part turns about */
    int32_t rate = 0;
    uint32_t run = 0, ramp = 0, stop = 0, count = 0;
    switch (entry->id)
    {
    case 203: /* gear, test cylinder, rotary bridge: ramp, run, ramp, rest, reverse */
    case 206:
    case 209:
        rate = turn_rate(s1lo) * ((object->settings[2] & 0xFFFFu) == 0 ? 1 : -1);
        run = s1hi;
        ramp = run ? s2lo : 0;
        stop = run ? s2hi : 0;
        break;
    case 208: /* rotary room: steady turn */
        rate = -turn_rate(s1lo);
        break;
    case 205: /* clock hand: steady, clockwise from above */
        rate = turn_rate(s1lo);
        break;
    case 204: /* drawbridge leaf: lifts s1lo degrees about its hinge, holds, lowers, rests */
        axis = 0;
        ramp = 60;
        run = 60;
        stop = s2hi;
        rate = mkds_round_div(((int64_t)s1lo << 32) / 360, ramp + run);
        break;
    case 207: /* pendulum: swings about its local z, peak speed s1lo/8192 rad/frame, half period s1hi */
    {
        const uint32_t amplitude = (uint32_t)mkds_round_div((int64_t)s1lo * s1hi * 405284735, 1000000000);
        if (!amplitude || amplitude > 16384 || !s1hi)
            ri_fail(c->m.ctx, "%s: pendulum settings %u/%u out of range", c->m.where, (unsigned)s1lo, (unsigned)s1hi);
        const int32_t vector[3] = {(int32_t)r[2] * 16, (int32_t)r[8] * 16, (int32_t)r[5] * 16};
        mkds_property(&c->m, "mksc:motion", MKDS_PROPERTY_STRING, mkds_string(&c->m, "swing"), 0);
        mkds_property_vector(&c->m, "mksc:motion-axis", vector);
        mkds_property(&c->m, "mksc:swing-amplitude", MKDS_PROPERTY_INTEGER, amplitude, 0);
        mkds_property(&c->m, "mksc:swing-period", MKDS_PROPERTY_INTEGER, 2 * s1hi, 0);
        return 4;
    }
    case 431: /* Fireball2: signed hundredths of two degrees per frame, local XY orbit. */
    {
        axis = 2;
        const int32_t step = 364 * (int16_t)s2lo / 100;
        if (step < -1024 || step > 1024)
            ri_fail(c->m.ctx, "%s: Fireball2 rotation speed exceeds motion bounds", c->m.where);
        rate = step * 65536;
        if (!rate)
            return 0;
        break;
    }
    case 202: /* koopa block: rides its PATH, each leg six frames per POIT duration unit, resting s1lo */
        *route = path_route(c, entry, object, map, 0);
        mkds_property(&c->m, "mksc:motion", MKDS_PROPERTY_STRING, mkds_string(&c->m, "route"), 0);
        mkds_property(&c->m, "mksc:route-wait", MKDS_PROPERTY_INTEGER, s1lo, 0);
        return 2;
    default:
        if (entry->family != MAPOBJ_HAZARD)
            ri_fail(c->m.ctx, "%s: %s has no motion", c->m.where, entry->name);
        return hazard_motion(c, entry, object, r, map, route);
    }
    if (!rate)
        ri_fail(c->m.ctx, "%s: %s does not move (settings %08x)", c->m.where, entry->name,
                (unsigned)object->settings[0]);
    const int32_t vector[3] = {(int32_t)r[axis] * 16, (int32_t)r[6 + axis] * 16, (int32_t)r[3 + axis] * 16};
    mkds_property(&c->m, "mksc:motion", MKDS_PROPERTY_STRING, mkds_string(&c->m, "spin"), 0);
    mkds_property_vector(&c->m, "mksc:motion-axis", vector);
    mkds_property(&c->m, "mksc:spin-rate", MKDS_PROPERTY_INTEGER, (uint32_t)rate, 0);
    count = 3;
    if (run)
    {
        mkds_property(&c->m, "mksc:spin-run", MKDS_PROPERTY_INTEGER, run, 0);
        mkds_property(&c->m, "mksc:spin-ramp", MKDS_PROPERTY_INTEGER, ramp, 0);
        mkds_property(&c->m, "mksc:spin-stop", MKDS_PROPERTY_INTEGER, stop, 0);
        count += 3;
    }
    return count;
}

/* Mesh vertex `vertex` at the object's rest pose: position + r (scale * local). */
static void rest_vertex(const ObjectMesh *mesh, uint32_t vertex, const int32_t position[3], const int64_t r[9],
                        const int32_t scale[3], int32_t out[3])
{
    const uint8_t *row = mesh->vertices + vertex * 20u;
    int64_t local[3];
    for (unsigned axis = 0; axis < 3; ++axis)
        local[axis] = (int64_t)(int32_t)ri_le32(row + axis * 4) * scale[axis] / 65536;
    for (unsigned axis = 0; axis < 3; ++axis)
        out[axis] = (int32_t)(position[axis] + mkds_round_div(r[axis * 3] * local[0] + r[axis * 3 + 1] * local[1] +
                                                                  r[axis * 3 + 2] * local[2],
                                                              4096));
}

/* Squared distance in 1/4096-metre units. */
static int64_t span_squared(const int32_t a[3], const int32_t b[3])
{
    int64_t sum = 0;
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        const int64_t d = ((int64_t)a[axis] - b[axis]) >> 4;
        sum += d * d;
    }
    return sum;
}

/* A hazard's contact sphere at rest, as OBJS properties: it covers the
 * vertices in the outer quarter of the mesh's reach from the object's
 * origin (a pendulum's bob, most of a squat enemy). Returns 2. */
static uint32_t hazard_sphere(Course *c, const MapObj *entry, const ObjectMesh *mesh, const int32_t position[3],
                              const int64_t r[9], const int32_t scale[3])
{
    int64_t reach = 0, sum[3] = {0, 0, 0}, count = 0, radius = 0;
    int32_t at[3], center[3];
    for (uint32_t vertex = 0; vertex < mesh->vertex_count; ++vertex)
    {
        rest_vertex(mesh, vertex, position, r, scale, at);
        const int64_t d = span_squared(at, position);
        reach = d > reach ? d : reach;
    }
    for (uint32_t vertex = 0; vertex < mesh->vertex_count; ++vertex)
    {
        rest_vertex(mesh, vertex, position, r, scale, at);
        if (span_squared(at, position) * 16 < reach * 9)
            continue;
        for (unsigned axis = 0; axis < 3; ++axis)
            sum[axis] += at[axis];
        ++count;
    }
    for (unsigned axis = 0; axis < 3; ++axis)
        center[axis] = (int32_t)(sum[axis] / count);
    for (uint32_t vertex = 0; vertex < mesh->vertex_count; ++vertex)
    {
        rest_vertex(mesh, vertex, position, r, scale, at);
        if (span_squared(at, position) * 16 >= reach * 9)
        {
            const int64_t d = span_squared(at, center);
            radius = d > radius ? d : radius;
        }
    }
    radius = (int64_t)portable_fixed_3d_sqrt((uint64_t)radius) << 4;
    if (radius <= 0 || radius > 64 * 65536)
        ri_fail(c->m.ctx, "%s: %s contact radius %d out of range", c->m.where, entry->name, (int)radius);
    mkds_property(&c->m, "mksc:hazard-radius", MKDS_PROPERTY_FIXED, (uint32_t)radius, 0);
    mkds_property_vector(&c->m, "mksc:hazard-center", center);
    if (entry->reaction == REACT_BUMP)
        return 2;
    mkds_property(&c->m, "mksc:hazard-reaction", MKDS_PROPERTY_STRING,
                  mkds_string(&c->m, reaction_names[entry->reaction]), 0);
    return 3;
}

/* MapObjs whose MapObj folder carries a stencil shadow volume, by the file
 * the game names after each (an object without one casts none). */
static const struct
{
    uint16_t id;
    const char *volume;
} object_shadows[] = {
    {207, "pendulum_shadow"}, {315, "teresa_shadow"}, {316, "baku_shadow"},  {402, "rock_shadow"},
    {403, "dossun_shadow"},   {405, "bus_a_shadow"},  {410, "car_a_shadow"}, {412, "truck_a_shadow"},
    {433, "rock2_shadow"},
};

/* Loads the family's shadow volume into c->shadow: the volume's plan (see
 * mkds_shadow_plan) as a kind-97 material at its colour and alpha. */
static void object_shadow(Course *c, const NdsFs *archive, const MapObj *entry)
{
    c->shadow.material = 0;
    for (size_t i = 0; i < sizeof(object_shadows) / sizeof(object_shadows[0]); ++i)
    {
        char path[64];
        RiData file;
        snprintf(path, sizeof(path), "MapObj/%s.nsbmd", object_shadows[i].volume);
        if (object_shadows[i].id != entry->id || !nds_find(archive, path, &file))
            continue;
        char where[96];
        snprintf(where, sizeof(where), "%s MapObj %s", c->m.where, path);
        const NitroModels models = nitro_bmd(c->m.ctx, file, path);
        if (models.count != 1u || models.models[0].material_count != 1u)
            ri_fail(c->m.ctx, "%s: not one single-material volume", where);
        const NitroModel *model = &models.models[0];
        RiBuf *pixels = mkds_shadow_plan(c->m.ctx, where, model, 0, c->shadow.low, c->shadow.high);
        /* The volumes are opaque black, which the game must thin at draw
         * time (untraced); they darken the floor as much as the karts' grey
         * opaque volumes do (0x636363: to 39%). */
        const NitroMaterial *material = &model->materials[0];
        const uint32_t colour = mkds_rgb_word(material->diffuse_ambient & 0x7FFFu); /* RGBA */
        const uint32_t alpha = (material->polygon_attr >> 16 & 31u) * 0x9Cu / 31u;
        const uint32_t slot = c->object_mesh_count++;
        if (slot == 256)
            ri_fail(c->m.ctx, "%s: more than 256 MapObj models", c->m.where);
        c->shadow.material = MKDS_OBJECT_MATERIAL_ID(c->source->index, slot, 0);
        RiBuf *image = ri_image_asset(c->m.ctx, MKDS_SHADOW_TEXELS, MKDS_SHADOW_TEXELS, pixels, 0, 0);
        ri_section(c->m.ctx, MKDS_KIND_MATERIAL, c->shadow.material, 1,
                   mkds_material_section(&c->m, alpha << 24 | colour >> 8, 0u, image));
        ri_buf_free(c->m.ctx, image);
        ri_buf_free(c->m.ctx, pixels);
        return;
    }
}

/* The mksc:shadow properties of a placement at scale (Q16, engine axes):
 * the decal's half extents and the depth below the origin the volume
 * reaches, in metres. Returns their count. */
static uint32_t shadow_properties(Course *c, const int32_t scale[3])
{
    if (!c->shadow.material)
        return 0;
    const int32_t *low = c->shadow.low, *high = c->shadow.high;
    const int32_t size[3] = {
        (int32_t)mkds_q16_mul(mkds_metres(&c->m, (high[0] - low[0]) / 2), scale[0]),
        (int32_t)mkds_q16_mul(mkds_metres(&c->m, (high[2] - low[2]) / 2), scale[1]),
        (int32_t)mkds_q16_mul(mkds_metres(&c->m, -low[1]), scale[2])};
    mkds_property(&c->m, "mksc:shadow", MKDS_PROPERTY_INTEGER, c->shadow.material, 0);
    mkds_property_vector(&c->m, "mksc:shadow-size", size);
    return 2;
}

/* A moving or animated MapObj: an OBJS row drawing its mesh at the OBJI
 * pose, its motion as properties, and the mesh's faces at that rest pose as
 * collision the row owns (a hazard instead hits karts touching its contact
 * sphere; animated scenery has neither). */
static void moving_object(Course *c, const MapObj *entry, const MkdsObject *object, const MkdsMap *map,
                          const ObjectMesh *mesh, const int32_t *pivot)
{
    MkdsPlacement place;
    placement(object, &place);
    int64_t r[9]; /* engine-axis rotation, Q12 */
    for (unsigned row = 0; row < 3; ++row)
        for (unsigned col = 0; col < 3; ++col)
            r[row * 3 + col] = place.r[engine_axis[row] * 3 + engine_axis[col]];
    int32_t position[3], quaternion[4], scale[3];
    engine_point(c, object->position, position);
    position[2] += c->sway_lift;
    matrix_quaternion(r, quaternion);
    for (unsigned axis = 0; axis < 3; ++axis)
        scale[axis] = object->scale[engine_axis[axis]] * 16;
    const uint32_t owner = c->m.tables[MKDS_T_OBJS].count;
    const uint32_t properties = c->m.tables[MKDS_T_PROP].count;
    uint32_t route = MKDS_ABSENT, property_count = 0;
    /* motion() receives the DS-axis matrix: its axis columns map to engine axes itself.
     * Animated scenery stands still and owns no collision. */
    if (entry->family != MAPOBJ_SCENERY)
        property_count = motion(c, entry, object, place.r, map, &route);
    if (pivot)
    {
        mkds_property_vector(&c->m, "mksc:motion-pivot", pivot);
        ++property_count;
    }
    if (entry->family == MAPOBJ_HAZARD)
        property_count += hazard_sphere(c, entry, mesh, position, r, scale);
    if (mesh->billboard)
    {
        mkds_property(&c->m, "mksc:billboard", MKDS_PROPERTY_BOOLEAN, 1, 0);
        ++property_count;
    }
    property_count += shadow_properties(c, scale);
    if (c->sway_link && property_count)
    {
        /* ARM9 0x020a73b0/0x020a79c4: each segment bends 0x71c turns<<16
         * times sin of a phase advancing 0x2d8 a tick (a 90-tick period),
         * segment j starting 20j ticks late; the matrix turns about the
         * object's Z per segment, so -X takes the sideways sway. */
        int32_t axis[3] = {-(int32_t)r[0] * 16, -(int32_t)r[3] * 16, 0};
        const int64_t wide[3] = {axis[0], axis[1], 0};
        if (!portable_fixed_3d_normalize(wide, axis))
            ri_fail(c->m.ctx, "%s: %s faces straight up", c->m.where, entry->name);
        for (unsigned k = 0; k < 3; ++k)
            axis[k] = (int32_t)((int64_t)axis[k] >> 14);
        mkds_property(&c->m, "mksc:sway-link", MKDS_PROPERTY_INTEGER, c->sway_link - 1, 0);
        mkds_property_vector(&c->m, "mksc:sway-axis", axis);
        mkds_property(&c->m, "mksc:sway-amplitude", MKDS_PROPERTY_INTEGER, 0x71c, 0);
        mkds_property(&c->m, "mksc:sway-period", MKDS_PROPERTY_INTEGER, 90, 0);
        mkds_property(&c->m, "mksc:sway-lag", MKDS_PROPERTY_INTEGER, 20, 0);
        mkds_property(&c->m, "mksc:sway-length", MKDS_PROPERTY_FIXED,
                      (uint32_t)mkds_units(&c->m, (int64_t)20 * object->scale[1]), 0);
        property_count += 6;
    }
    const uint32_t row[16] = {entry->id, MKDS_KIND_MESH, mesh->id, (uint32_t)position[0], (uint32_t)position[1],
                              (uint32_t)position[2], (uint32_t)quaternion[0], (uint32_t)quaternion[1],
                              (uint32_t)quaternion[2], (uint32_t)quaternion[3], (uint32_t)scale[0], (uint32_t)scale[1],
                              (uint32_t)scale[2], route, properties, property_count};
    mkds_words(&c->m, MKDS_T_OBJS, row, 16);
    for (uint32_t face = 0; entry->family == MAPOBJ_PLATFORM && face < mesh->triangle_count; ++face)
    {
        uint32_t words[10];
        for (unsigned k = 0; k < 3; ++k)
        {
            int32_t at[3];
            rest_vertex(mesh, mesh->triangles[face * 4 + k], position, r, scale, at);
            for (unsigned axis = 0; axis < 3; ++axis)
                words[k * 3 + axis] = (uint32_t)at[axis];
        }
        words[9] = owner;
        for (unsigned k = 0; k < 10; ++k)
            ri_u32(c->m.ctx, c->platform_faces, words[k]);
        c->platform_face_count++;
    }
}

/* Fireball2 settings are arm count, radius in map units, signed angular
 * speed and balls-per-arm minus one (MarioKartToolbox Fireball2). Expand the
 * assembly into ordinary hazard objects sharing an explicit orbit pivot;
 * both collision and rendering then use the same general spin pose. */
static void fireball_ring(Course *c, const MapObj *entry, const MkdsObject *object,
                          const MkdsMap *map, const ObjectMesh *mesh)
{
    const unsigned arms = object->settings[0] & 0xffffu;
    const int radius = (int16_t)(object->settings[0] >> 16);
    const unsigned balls = (object->settings[1] >> 16) + 1u;
    if (!arms || arms > 20 || !balls || balls > 20 || radius <= 0)
        ri_fail(c->m.ctx, "%s: Fireball2 invalid arms/radius/balls %u/%d/%u",
                c->m.where, arms, radius, balls);
    MkdsPlacement place;
    placement(object, &place);
    int32_t pivot[3];
    engine_point(c, object->position, pivot);
    for (unsigned ball = 0; ball < balls; ++ball)
        for (unsigned arm = 0; arm < arms; ++arm)
        {
            int32_t sine, cosine;
            portable_fixed_3d_sincos((uint16_t)((arm + 1u) * (65536u / arms)), &sine, &cosine);
            const int64_t reach = (int64_t)radius * 4096 * (balls - ball) / balls;
            const int64_t x = reach * cosine / 1073741824, y = reach * sine / 1073741824;
            MkdsObject part = *object;
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                const int64_t position = (int64_t)object->position[axis] +
                    mkds_round_div(place.r[axis * 3] * x + place.r[axis * 3 + 1] * y, 4096);
                if (position < INT32_MIN || position > INT32_MAX)
                    ri_fail(c->m.ctx, "%s: Fireball2 orbit exceeds position bounds", c->m.where);
                part.position[axis] = (int32_t)position;
            }
            moving_object(c, entry, &part, map, mesh, pivot);
        }
    if (balls > 1)
    {
        MkdsObject center = *object;
        center.settings[1] &= 0xffff0000u; /* the center does not orbit */
        moving_object(c, entry, &center, map, mesh, NULL);
    }
}

/* A SOLID MapObj's wall: an upright octagonal prism around the course
 * vertices its placement just added (from `first`), its radius
 * entry->solid_percent of the narrower half width (the wider for a flat
 * card, under a quarter as deep as wide). */
static void solid_prism(Course *c, const MapObj *entry, uint32_t first)
{
    const MkdsTable *vert = &c->m.tables[MKDS_T_VERT];
    int32_t low[3] = {INT32_MAX, INT32_MAX, INT32_MAX}, high[3] = {INT32_MIN, INT32_MIN, INT32_MIN};
    for (uint32_t index = first; index < vert->count; ++index)
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            const int32_t v = (int32_t)ri_le32(vert->bytes->data + index * 32 + axis * 4);
            low[axis] = v < low[axis] ? v : low[axis];
            high[axis] = v > high[axis] ? v : high[axis];
        }
    if (first >= vert->count || high[2] <= low[2])
        ri_fail(c->m.ctx, "%s: %s has no extent for its wall", c->m.where, entry->name);
    const int64_t x_span = (int64_t)high[0] - low[0], y_span = (int64_t)high[1] - low[1];
    const int64_t narrow = x_span < y_span ? x_span : y_span, wide = x_span < y_span ? y_span : x_span;
    const int64_t half = (narrow * 4 < wide ? wide : narrow) / 2;
    const int64_t radius = half * entry->solid_percent / 100;
    if (radius < 4096)
        ri_fail(c->m.ctx, "%s: %s wall radius is under 1/16 m", c->m.where, entry->name);
    /* cos/sin of k*45 degrees, Q16 */
    static const int32_t unit[8][2] = {{65536, 0},  {46341, 46341},   {0, 65536},  {-46341, 46341},
                                       {-65536, 0}, {-46341, -46341}, {0, -65536}, {46341, -46341}};
    const int64_t x = ((int64_t)low[0] + high[0]) / 2, y = ((int64_t)low[1] + high[1]) / 2;
    for (unsigned k = 0; k < 8; ++k)
    {
        const unsigned n = (k + 1) % 8;
        const int32_t a[2] = {(int32_t)(x + radius * unit[k][0] / 65536), (int32_t)(y + radius * unit[k][1] / 65536)};
        const int32_t b[2] = {(int32_t)(x + radius * unit[n][0] / 65536), (int32_t)(y + radius * unit[n][1] / 65536)};
        const int32_t faces[2][9] = {{a[0], a[1], low[2], b[0], b[1], low[2], b[0], b[1], high[2]},
                                     {a[0], a[1], low[2], b[0], b[1], high[2], a[0], a[1], high[2]}};
        for (unsigned f = 0; f < 2; ++f)
        {
            for (unsigned w = 0; w < 9; ++w)
                ri_u32(c->m.ctx, c->solid_walls, (uint32_t)faces[f][w]);
            c->solid_wall_count++;
        }
    }
}

/* Sound-emitting MapObjs become kind-3 ZONE rows at their placed position,
 * the audible radius as each half-extent (mkds_object_sound). */
static void sound_zones(Course *c, const MkdsMap *map)
{
    for (size_t index = 0; index < map->object_count; ++index)
    {
        const MkdsObject *object = &map->objects[index];
        const MkdsObjectSound *sound = mkds_object_sound(c->source->folder, object->id);
        if (!sound)
            continue;
        int32_t at[3];
        engine_point(c, object->position, at);
        const int32_t radius = (int32_t)sound->radius * 65536;
        const uint32_t property = c->m.tables[MKDS_T_PROP].count;
        mkds_property(&c->m, "mksc:sound", MKDS_PROPERTY_INTEGER, 0x8000u | MKDS_EFFECT_ID(0, sound->effect), 0);
        mkds_property(&c->m, "mksc:sound-period", MKDS_PROPERTY_INTEGER, sound->period, 0);
        const uint32_t row[20] = {(uint32_t)at[0], (uint32_t)at[1], (uint32_t)at[2], (uint32_t)radius,
                                  (uint32_t)radius, (uint32_t)radius, 0, 0, 0, 1u << 30, 0, 0, 0, 3u, 0, 0, 0,
                                  property, 2u, 0};
        mkds_words(&c->m, MKDS_T_ZONE, row, 20);
    }
}

/* A projectile thrown along a PATH (a Piranha Plant's fireball, the sun's
 * fire snake): it runs the PATH at `speed` metres/second, flies straight back
 * to `hold` (the plant's mouth, the sun) in a few ticks, rests there `pause`
 * ticks and is thrown again; `phase` ticks staggers several throwers. */
static void projectile(Course *c, const MapObj *entry, const ObjectMesh *mesh, const MkdsMap *map, uint32_t rail_index,
                       const int32_t hold[3], uint32_t speed, uint32_t pause, uint32_t phase, uint32_t scale)
{
    if (rail_index >= map->rail_count || map->rails[rail_index].length < 2 || map->rails[rail_index].length > 30)
        ri_fail(c->m.ctx, "%s: %s has no throwing PATH %u", c->m.where, entry->name, (unsigned)rail_index);
    const MkdsRail *rail = &map->rails[rail_index];
    int32_t points[32][3];
    int64_t speeds[32];
    uint32_t count = 0;
    for (uint32_t k = 0; k < rail->length; ++k, ++count)
    {
        engine_point(c, map->rail_points[rail->start + k], points[count]);
        speeds[count] = (int64_t)speed << 16;
    }
    speeds[count - 1] = 64 << 16; /* straight back */
    memcpy(points[count], hold, sizeof points[count]);
    /* Resting: a one-centimetre leg over `pause` ticks of 4389/262144 s. */
    speeds[count++] = (int64_t)655 * 262144 / ((int64_t)pause * 4389);
    memcpy(points[count], hold, sizeof points[count]);
    points[count][2] += 655;
    speeds[count++] = 64 << 16;
    /* Route offsets are relative to point 0: the row stands at the hold. */
    int32_t rotated[32][3];
    int64_t rotated_speeds[32];
    for (uint32_t k = 0; k < count; ++k)
    {
        memcpy(rotated[k], points[(k + count - 2) % count], sizeof rotated[k]);
        rotated_speeds[k] = speeds[(k + count - 2) % count];
    }
    const uint32_t properties = c->m.tables[MKDS_T_PROP].count;
    const uint32_t route = object_route(c, (const int32_t (*)[3])rotated, rotated_speeds, count);
    mkds_property(&c->m, "mksc:motion", MKDS_PROPERTY_STRING, mkds_string(&c->m, "route"), 0);
    mkds_property(&c->m, "mksc:route-wait", MKDS_PROPERTY_INTEGER, 0, 0);
    mkds_property(&c->m, "mksc:motion-phase", MKDS_PROPERTY_INTEGER, phase, 0);
    const int64_t r[9] = {4096, 0, 0, 0, 4096, 0, 0, 0, 4096};
    const int32_t scales[3] = {(int32_t)scale, (int32_t)scale, (int32_t)scale};
    const uint32_t sphere = hazard_sphere(c, entry, mesh, rotated[0], r, scales);
    uint32_t count_props = 3 + sphere;
    if (mesh->billboard)
    {
        mkds_property(&c->m, "mksc:billboard", MKDS_PROPERTY_BOOLEAN, 1, 0);
        ++count_props;
    }
    const uint32_t row[16] = {entry->id, MKDS_KIND_MESH, mesh->id, (uint32_t)rotated[0][0], (uint32_t)rotated[0][1],
                              (uint32_t)rotated[0][2], 0, 0, 0, 1u << 30, scale, scale, scale, route, properties,
                              count_props};
    mkds_words(&c->m, MKDS_T_OBJS, row, 16);
}

/* The fireballs a Piranha Plant spits (PakkunFire): one along each PATH its
 * settings name (setting1 and setting2 halves, 0xFFFF none), in turn. */
static void piranha_fire(Course *c, const NdsFs *archive, const NdsFs *texture_archive, const MkdsObject *object,
                         const MkdsMap *map)
{
    static const MapObj fire = {427, MAPOBJ_HAZARD, "PakkunFire", {"FireBall"}, 0, 0, 0, 0, REACT_SPIN};
    MkdsResources model;
    if (!mkds_resources(&c->m, archive, texture_archive, "MapObj/FireBall", &model))
        ri_fail(c->m.ctx, "%s: Piranha Plant needs MapObj/FireBall.nsbmd", c->m.where);
    const ObjectMesh mesh = object_mesh(c, &model, "FireBall", NULL);
    const uint32_t rails[4] = {object->settings[0] & 0xFFFFu, object->settings[0] >> 16,
                               object->settings[1] & 0xFFFFu, object->settings[1] >> 16};
    int32_t mouth[3];
    engine_point(c, object->position, mouth);
    mouth[2] += 3 << 16;
    for (unsigned k = 0; k < 4; ++k)
        if (rails[k] != 0xFFFFu)
            projectile(c, &fire, &mesh, map, rails[k], mouth, 10, 240, k * 90, 65536);
}

/* The sun's fire snakes (SunFireSnake): thrown along every PATH no other
 * placement rides, in turn, and back into the sun between throws. */
static void sun_fire(Course *c, const NdsFs *archive, const NdsFs *texture_archive, const MkdsObject *sun,
                     const MkdsMap *map)
{
    static const MapObj snake = {430, MAPOBJ_HAZARD, "SunFireSnake", {"sun_LOD"}, 0, 0, 0, 0, REACT_SPIN};
    MkdsResources model;
    if (!mkds_resources(&c->m, archive, texture_archive, "MapObj/sun_LOD", &model))
        ri_fail(c->m.ctx, "%s: the sun needs MapObj/sun_LOD.nsbmd", c->m.where);
    const ObjectMesh mesh = object_mesh(c, &model, "sun_LOD", NULL);
    int32_t at[3];
    engine_point(c, sun->position, at);
    uint32_t thrown = 0;
    for (uint32_t rail = 0; rail < map->rail_count && rail < 64; ++rail)
    {
        int ridden = 0;
        for (size_t index = 0; index < map->object_count; ++index)
            ridden |= map->objects[index].route == (int32_t)rail;
        if (ridden || map->rails[rail].loop)
            continue;
        projectile(c, &snake, &mesh, map, rail, at, 8, 360, thrown * 97, 16384);
        ++thrown;
    }
}

/* A Chain Chomp's chain: links evenly spaced from its post to the Chomp,
 * each riding a share of the Chomp's lunge so the chain stretches with it. */
#define CHAIN_LINKS 6
static void chain_links(Course *c, const NdsFs *archive, const NdsFs *texture_archive, const MapObj *entry,
                        const MkdsObject *object, const MkdsMap *map)
{
    MkdsResources model;
    if (!mkds_resources(&c->m, archive, texture_archive, "MapObj/wanwan_chain", &model))
        ri_fail(c->m.ctx, "%s: %s needs MapObj/wanwan_chain.nsbmd", c->m.where, entry->name);
    const ObjectMesh mesh = object_mesh(c, &model, "wanwan_chain", NULL);
    int32_t post[3], rest[3], lunge[3];
    lunge_points(c, object, map, post, rest, lunge);
    for (unsigned link = 1; link <= CHAIN_LINKS; ++link)
    {
        const int64_t share = 65536 * (int64_t)link / (CHAIN_LINKS + 1);
        int32_t from[3], to[3];
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            from[axis] = post[axis] + (int32_t)(((int64_t)rest[axis] - post[axis]) * share >> 16);
            to[axis] = post[axis] + (int32_t)(((int64_t)lunge[axis] - post[axis]) * share >> 16);
        }
        const uint32_t properties = c->m.tables[MKDS_T_PROP].count;
        const uint32_t route = lunge_route(c, from, to, entry->speed, share);
        const uint32_t scale = (uint32_t)object->scale[0] * 16;
        const uint32_t row[16] = {407, MKDS_KIND_MESH, mesh.id, (uint32_t)from[0], (uint32_t)from[1], (uint32_t)from[2],
                                  0, 0, 0, 1u << 30, scale, scale, scale, route, properties, 2};
        mkds_words(&c->m, MKDS_T_OBJS, row, 16);
    }
}

/* Particle MapObjs become kind-4 ZONE rows at their placed pose; integer
 * mksc:emitter names the DS effect set's emitter row (mkds_fx.c). */
/* Scenery MapObjs join the course mesh at their OBJI scale, rotation (Y, X
 * then Z) and position, one material set per model file; moving and
 * bone-animated ones become OBJS rows over one mesh per model file. */
static void scenery(Course *c, const NdsFs *archive, const NdsFs *texture_archive, const MkdsMap *map)
{
    for (size_t kind = 0; kind < sizeof(map_objects) / sizeof(map_objects[0]); ++kind)
    {
        const MapObj *entry = &map_objects[kind];
        if (entry->family != MAPOBJ_SCENERY && entry->family != MAPOBJ_VARIANTS &&
            entry->family != MAPOBJ_SOLID && entry->family != MAPOBJ_PLATFORM &&
            entry->family != MAPOBJ_HAZARD)
            continue;
        size_t placed = 0;
        for (size_t index = 0; index < map->object_count; ++index)
            placed += map->objects[index].id == entry->id;
        for (unsigned part = 0; placed && part < 2 && entry->models[part]; ++part)
        {
            MkdsResources model;
            char stem[48];
            snprintf(stem, sizeof(stem), "MapObj/%s", entry->models[part]);
            if (!mkds_resources(&c->m, archive, texture_archive, stem, &model) &&
                !(texture_archive && mkds_resources(&c->m, texture_archive, NULL, stem, &model)))
                ri_fail(c->m.ctx, "%s: MapObj %s needs %s.nsbmd", c->m.where, entry->name, stem);
            /* Bone-animated scenery and hazards draw their own rigged mesh.
             * Platforms do not: their collision follows mksc:motion alone. */
            NitroJointAnimation animation;
            const int animated = (entry->family == MAPOBJ_SCENERY || entry->family == MAPOBJ_HAZARD) &&
                                 object_animation(c, archive, texture_archive, stem, &animation);
            if (entry->family == MAPOBJ_PLATFORM || entry->family == MAPOBJ_HAZARD || animated)
            {
                if (part == 0)
                    object_shadow(c, archive, entry);
                const ObjectMesh mesh = object_mesh(c, &model, entry->models[part], animated ? &animation : NULL);
                /* A Chain Chomp's post stands still beside it. */
                MapObj post = *entry;
                post.family = MAPOBJ_SCENERY;
                const int still = entry->motion == HAZARD_LUNGE && part == 1;
                for (size_t index = 0; index < map->object_count; ++index)
                    if (map->objects[index].id == entry->id)
                    {
                        if (entry->id == 431)
                            fireball_ring(c, entry, &map->objects[index], map, &mesh);
                        else if (entry->motion == HAZARD_LUNGE && part == 0)
                        {
                            /* The Chomp waits a quarter of its chain out from the post. */
                            int32_t at[3], rest[3], lunge[3];
                            lunge_points(c, &map->objects[index], map, at, rest, lunge);
                            MkdsObject chomp = map->objects[index];
                            chomp.position[0] = (int32_t)mkds_round_div((int64_t)rest[0] * MKDS_UNITS_PER_METRE, 16);
                            chomp.position[2] = (int32_t)mkds_round_div((int64_t)rest[1] * MKDS_UNITS_PER_METRE, 16);
                            c->chained = &map->objects[index];
                            moving_object(c, entry, &chomp, map, &mesh, NULL);
                            c->chained = NULL;
                        }
                        else if (entry->id == 434)
                        {
                            /* A Pokey (ARM9 0x020a79c4): three sanbo_b segments
                             * and the sanbo_h head on top, 10 then 20 more map
                             * units apart in the object's scale. */
                            for (uint32_t link = part ? 3 : 0; link < (part ? 4u : 3u); ++link)
                            {
                                c->sway_lift = mkds_units(&c->m, (10 + 20 * (int64_t)link) * map->objects[index].scale[1]);
                                c->sway_link = link + 1;
                                moving_object(c, entry, &map->objects[index], map, &mesh, NULL);
                            }
                            c->sway_link = 0;
                            c->sway_lift = 0;
                        }
                        else
                            moving_object(c, still ? &post : entry, &map->objects[index], map, &mesh, NULL);
                        if (entry->motion == HAZARD_LUNGE && part == 0)
                            chain_links(c, archive, texture_archive, entry, &map->objects[index], map);
                        if (entry->id == 426 && part == 0)
                            piranha_fire(c, archive, texture_archive, &map->objects[index], map);
                    }
                c->shadow.material = 0;
                continue;
            }
            uint32_t first_material = c->m.tables[MKDS_T_MATS].count;
            uint32_t *variants = NULL;
            if (entry->family == MAPOBJ_VARIANTS)
            {
                if (!model.has_pattern || !model.pattern.duration)
                    ri_fail(c->m.ctx, "%s: %s needs an appearance pattern", c->m.where, entry->name);
                RiBuf *cache = ri_buf_zero(c->m.ctx, model.pattern.duration * sizeof(uint32_t));
                memset(cache->data, 0xff, cache->size);
                variants = (uint32_t *)(void *)cache->data;
            }
            else
                mkds_materials(&c->m, &model.models.models[0], &model.textures, model.has_srt ? &model.srt : NULL,
                               model.has_pattern ? &model.pattern : NULL, 0);
            uint32_t ordinal = 0;
            for (size_t index = 0; index < map->object_count; ++index)
            {
                const MkdsObject *object = &map->objects[index];
                if (object->id != entry->id)
                    continue;
                if (variants)
                {
                    /* Setting 1 selects a one-based appearance; zero is automatic.
                     * Cycle automatic placements deterministically at import, not
                     * at render time, without consuming the race's random stream. */
                    const uint32_t choice = object->settings[0] & 0xffffu;
                    if (choice > model.pattern.duration)
                        ri_fail(c->m.ctx, "%s: %s appearance %u exceeds %u frames",
                                c->m.where, entry->name, choice, model.pattern.duration);
                    const uint32_t frame = choice ? choice - 1 : ordinal % model.pattern.duration;
                    if (variants[frame] == MKDS_ABSENT)
                    {
                        variants[frame] = c->m.tables[MKDS_T_MATS].count;
                        mkds_materials_at_frame(&c->m, &model.models.models[0], &model.textures,
                            model.has_srt ? &model.srt : NULL, &model.pattern, 0, frame);
                    }
                    first_material = variants[frame];
                    ++ordinal;
                }
                MkdsPlacement place;
                placement(object, &place);
                c->m.placement = &place;
                const uint32_t first_vertex = c->m.tables[MKDS_T_VERT].count;
                const int billboard = mkds_geometry(&c->m, &model.models.models[0], first_material, 0);
                if (entry->family == MAPOBJ_SOLID && part == 0)
                    solid_prism(c, entry, first_vertex);
                if (billboard)
                {
                    /* Camera-facing parts become crossed planes: the model as
                     * placed and a copy turned a quarter about its Y. */
                    int64_t turned[9];
                    for (unsigned row = 0; row < 3; ++row)
                    {
                        turned[row * 3] = -place.r[row * 3 + 2];
                        turned[row * 3 + 1] = place.r[row * 3 + 1];
                        turned[row * 3 + 2] = place.r[row * 3];
                    }
                    memcpy(place.r, turned, sizeof(turned));
                    mkds_geometry(&c->m, &model.models.models[0], first_material, 0);
                }
                c->m.placement = NULL;
            }
        }
    }
    for (size_t index = 0; index < map->object_count; ++index)
        if (map->objects[index].id == 429)
            sun_fire(c, archive, texture_archive, &map->objects[index], map);
}

/* The MapObj families this course places but does not convert yet, for
 * META mksc:incomplete ("" when complete). */
static const char *incomplete(Course *c, const MkdsMap *map)
{
    RiBuf *text = ri_buf(c->m.ctx);
    for (size_t kind = 0; kind < sizeof(map_objects) / sizeof(map_objects[0]); ++kind)
    {
        const MapObj *entry = &map_objects[kind];
        size_t placed = 0;
        for (size_t index = 0; index < map->object_count; ++index)
            placed += map_object(c, &map->objects[index], index) == entry;
        if (!placed || entry->family == MAPOBJ_ITEM || entry->family == MAPOBJ_SILENT ||
            entry->family == MAPOBJ_SCENERY || entry->family == MAPOBJ_VARIANTS ||
            entry->family == MAPOBJ_SOLID || entry->family == MAPOBJ_PLATFORM ||
            entry->family == MAPOBJ_HAZARD || entry->family == MAPOBJ_EFFECT)
            continue;
        if (text->size)
            ri_put(c->m.ctx, text, ", ", 2);
        ri_put(c->m.ctx, text, entry->name, strlen(entry->name));
    }
    ri_u8(c->m.ctx, text, 0);
    return (const char *)text->data;
}

/* A Nitro screen's opaque crop as a course texture: NCLR, NCGR and NSCR
 * named `stem` plus the suffixes. */
static uint32_t screen_image(Course *c, const NdsFs *fs, const char *palette, const char *characters,
                             const char *screen)
{
    char where[160];
    snprintf(where, sizeof(where), "%s/%s", fs->name, screen);
    const Nitro2DImage image = nitro_screen(c->m.ctx, nds_file(c->m.ctx, fs, palette), nds_file(c->m.ctx, fs, characters),
                                            nds_file(c->m.ctx, fs, screen), where);
    return mkds_image(&c->m, image.pixels, image.width, image.height);
}

/* The minimap. The game places racers by a per-course world rectangle and
 * screen window in its ARM9; the Map2D art (a 256x256 screen) is the record
 * of that transform here. It is recovered by fitting a quarter-turn, a
 * uniform scale and an offset that put every CPU route point on the art's
 * opaque pixels, keeping the middle of the scales that do. Rotated art is
 * turned so x runs right and z down, and the bounds are the crop's edges. */
typedef struct MapFit
{
    unsigned turn;  /* 0: x right, z down; 1: -z right, x down; 2: z right, -x down */
    int64_t scale;  /* pixels per 2^32 map units */
    int32_t ox, oy; /* screen pixel of the route centre */
} MapFit;

static int64_t floor_shift32(int64_t value)
{
    return value >= 0 ? value >> 32 : -((-value + 0xFFFFFFFFll) >> 32);
}

static void map_axes(unsigned turn, int64_t x, int64_t z, int64_t *a, int64_t *b)
{
    *a = turn == 0 ? x : turn == 1 ? -z : z;
    *b = turn == 0 ? z : turn == 1 ? x : -x;
}

/* Route points on `mask` (a 256x256 screen) for this fit, or -1 once
 * `floor` is out of reach. */
static int map_hits(const uint8_t *mask, const int32_t (*offset)[2], size_t count, int32_t ox, int32_t oy, int floor)
{
    int hits = 0;
    for (size_t index = 0; index < count; ++index)
    {
        const int32_t x = ox + offset[index][0], y = oy + offset[index][1];
        if (x >= 0 && y >= 0 && x < 256 && y < 256 && mask[y * 256 + x])
            ++hits;
        else if ((int)(count - index) - 1 + hits < floor)
            return -1;
    }
    return hits;
}

static void map_offsets(const MkdsMap *map, unsigned turn, int64_t cx, int64_t cz, int64_t scale, int32_t (*offset)[2])
{
    for (size_t index = 0; index < map->enemy_point_count; ++index)
    {
        int64_t a, b;
        map_axes(turn, map->enemy_points[index].position[0] - cx, map->enemy_points[index].position[2] - cz, &a, &b);
        offset[index][0] = (int32_t)floor_shift32(a * scale);
        offset[index][1] = (int32_t)floor_shift32(b * scale);
    }
}

static uint32_t minimap(Course *c, const MkdsMap *map, const Nitro2DImage *art, int32_t low[3], int32_t high[3])
{
    enum { SCALES = 320, REACH = 24 };
    const size_t count = map->enemy_point_count;
    if (count < 3)
        ri_fail(c->m.ctx, "%s: course_map.nkm has too few CPU points to place the minimap", c->m.where);
    if (art->left + art->width > 256u || art->top + art->height > 256u)
        ri_fail(c->m.ctx, "%s: Map2D minimap is larger than a 256x256 screen", c->m.where);
    int64_t box[4] = {INT64_MAX, INT64_MAX, INT64_MIN, INT64_MIN};
    for (size_t index = 0; index < count; ++index)
        for (unsigned axis = 0; axis < 2; ++axis)
        {
            const int64_t v = map->enemy_points[index].position[axis * 2];
            box[axis] = v < box[axis] ? v : box[axis];
            box[axis + 2] = v > box[axis + 2] ? v : box[axis + 2];
        }
    const int64_t cx = (box[0] + box[2]) / 2, cz = (box[1] + box[3]) / 2;
    const int64_t extent = box[2] - box[0] > box[3] - box[1] ? box[2] - box[0] : box[3] - box[1];
    if (extent <= 0)
        ri_fail(c->m.ctx, "%s: CPU points have no extent for the minimap", c->m.where);
    /* The route spans 48 to 256 pixels across. */
    const int64_t smallest = (48ll << 32) / extent, largest = (256ll << 32) / extent;
    /* Opaque pixels, and the same grown by one pixel for the coarse pass. */
    uint8_t *mask = ri_buf_zero(c->m.ctx, 2u * 256u * 256u)->data, *grown = mask + 256u * 256u;
    for (uint32_t y = 0; y < art->height; ++y)
        for (uint32_t x = 0; x < art->width; ++x)
            mask[(art->top + y) * 256u + art->left + x] = ri_pixel(art->pixels, (size_t)y * art->width + x) >> 24 != 0;
    for (int32_t y = 0; y < 256; ++y)
        for (int32_t x = 0; x < 256; ++x)
            for (int32_t dy = -1; dy <= 1; ++dy)
                for (int32_t dx = -1; dx <= 1; ++dx)
                    if (x + dx >= 0 && x + dx < 256 && y + dy >= 0 && y + dy < 256 && mask[(y + dy) * 256 + x + dx])
                        grown[y * 256 + x] = 1;
    int32_t (*offset)[2] = (int32_t (*)[2])(void *)ri_buf_zero(c->m.ctx, count * sizeof(*offset))->data;
    const int32_t centre_x = (int32_t)(art->left + art->width / 2), centre_y = (int32_t)(art->top + art->height / 2);

    /* Coarse: the quarter-turn whose best fit covers most points. */
    int best = -1;
    unsigned turn = 0;
    for (unsigned candidate = 0; candidate < 3; ++candidate)
        for (unsigned step = 0; step < SCALES; ++step)
        {
            map_offsets(map, candidate, cx, cz, smallest + (largest - smallest) * step / (SCALES - 1), offset);
            for (int32_t dy = -REACH; dy <= REACH; dy += 2)
                for (int32_t dx = -REACH; dx <= REACH; dx += 2)
                {
                    const int hits = map_hits(grown, (const int32_t (*)[2])offset, count, centre_x + dx, centre_y + dy, best + 1);
                    if (hits > best)
                    {
                        best = hits;
                        turn = candidate;
                    }
                }
        }
    /* Fine: every one-pixel offset on the exact art. The scales that fit
     * best form runs; take the run of the largest. Its middle is the scale,
     * unless the run reaches the smallest scale: the art is solid under the
     * route there (battle stages), and the largest scale is the one that
     * spans it. */
    int top = -1;
    int *scale_hits = (int *)(void *)ri_buf_zero(c->m.ctx, SCALES * sizeof(int))->data;
    for (unsigned step = 0; step < SCALES; ++step)
    {
        map_offsets(map, turn, cx, cz, smallest + (largest - smallest) * step / (SCALES - 1), offset);
        scale_hits[step] = -1;
        for (int32_t dy = -REACH; dy <= REACH; ++dy)
            for (int32_t dx = -REACH; dx <= REACH; ++dx)
            {
                const int hits = map_hits(mask, (const int32_t (*)[2])offset, count, centre_x + dx, centre_y + dy,
                                          scale_hits[step] > top ? scale_hits[step] : top);
                scale_hits[step] = hits > scale_hits[step] ? hits : scale_hits[step];
            }
        top = scale_hits[step] > top ? scale_hits[step] : top;
    }
    if (top * 5 < (int)count * 4)
        ri_fail(c->m.ctx, "%s: Map2D minimap fits only %d of %u CPU points", c->m.where, top, (unsigned)count);
    unsigned last = SCALES - 1;
    while (scale_hits[last] != top)
        --last;
    unsigned first = last;
    while (first > 0 && scale_hits[first - 1] == top)
        --first;
    const unsigned chosen = first == 0 ? last : (first + last) / 2;
    MapFit fit = {turn, smallest + (largest - smallest) * chosen / (SCALES - 1), 0, 0};
    map_offsets(map, turn, cx, cz, fit.scale, offset);
    int64_t sum[2] = {0, 0}, fits = 0;
    int here = -1;
    for (int32_t dy = -REACH; dy <= REACH; ++dy)
        for (int32_t dx = -REACH; dx <= REACH; ++dx)
        {
            const int hits = map_hits(mask, (const int32_t (*)[2])offset, count, centre_x + dx, centre_y + dy, here);
            if (hits > here)
            {
                here = hits;
                sum[0] = sum[1] = fits = 0;
            }
            if (hits == here)
            {
                sum[0] += dx;
                sum[1] += dy;
                ++fits;
            }
        }
    fit.ox = centre_x + mkds_round_div(sum[0], fits);
    fit.oy = centre_y + mkds_round_div(sum[1], fits);

    /* Pixel p covers axis values [(p - o) / scale, (p + 1 - o) / scale). */
    const int64_t unit = (int64_t)1 << 32;
    const int64_t a_low = ((int64_t)art->left - fit.ox) * unit / fit.scale,
                  a_high = ((int64_t)(art->left + art->width) - fit.ox) * unit / fit.scale,
                  b_low = ((int64_t)art->top - fit.oy) * unit / fit.scale,
                  b_high = ((int64_t)(art->top + art->height) - fit.oy) * unit / fit.scale;
    const int64_t x_low = fit.turn == 0 ? a_low : fit.turn == 1 ? b_low : -b_high,
                  x_high = fit.turn == 0 ? a_high : fit.turn == 1 ? b_high : -b_low,
                  z_low = fit.turn == 0 ? b_low : fit.turn == 1 ? -a_high : a_low,
                  z_high = fit.turn == 0 ? b_high : fit.turn == 1 ? -a_low : a_high;
    low[0] = mkds_units(&c->m, cx + x_low);
    high[0] = mkds_units(&c->m, cx + x_high);
    low[1] = mkds_units(&c->m, cz + z_low);
    high[1] = mkds_units(&c->m, cz + z_high);
    low[2] = high[2] = 0;
    if (fit.turn == 0)
        return mkds_image(&c->m, art->pixels, art->width, art->height);
    const uint32_t w = art->height, h = art->width;
    RiBuf *turned = ri_buf_zero(c->m.ctx, (size_t)w * h * 4u);
    for (uint32_t j = 0; j < h; ++j)
        for (uint32_t i = 0; i < w; ++i)
        {
            const uint32_t px = fit.turn == 1 ? art->width - 1 - j : j, py = fit.turn == 1 ? i : art->height - 1 - i;
            ri_set_pixel(turned, (size_t)j * w + i, ri_pixel(art->pixels, (size_t)py * art->width + px));
        }
    return mkds_image(&c->m, turned, w, h);
}

/* Kart size and race camera, fixed in the US ARM9 (map units, 20 a metre).
 * kartphysicalparam colSphereSize is 8 for the Standard kart: a 0.4 m
 * collision radius, 0.8 of the stock kart, and the karts drawn at 16 map
 * units per model unit match it at that scale. cam_setupRaceParams
 * (0x02077998) takes the 1P row of the table at 0x02154FA0: distance 58,
 * elevation 34 and target elevation 20. cam_updatePlayerCamera (0x02076528)
 * aims at the driver's position raised by the target elevation and keeps
 * the eye at the distance from the driver along (back 58, up 34); the
 * driver's position is its sphere's centre, 8 units over the road. The
 * camera's fov 5500 (0x0216569C, 1/65536 turns) is half the vertical view
 * (cam_updateFrustum, 0x02077060: top = near * tan fov). */
#define DS_KART_SPHERE 8
#define DS_CAMERA_DISTANCE 58
#define DS_CAMERA_ELEVATION 34
#define DS_CAMERA_TARGET_ELEVATION 20
#define DS_CAMERA_HALF_FOV 5500
static uint32_t chase_properties(MkdsModel *m)
{
    const double q = 65536.0 / MKDS_UNITS_PER_METRE, reach = hypot(DS_CAMERA_DISTANCE, DS_CAMERA_ELEVATION);
    const int32_t eye[3] = {0, (int32_t)-llround(DS_CAMERA_DISTANCE * DS_CAMERA_DISTANCE / reach * q),
                            (int32_t)llround((DS_KART_SPHERE + DS_CAMERA_DISTANCE * DS_CAMERA_ELEVATION / reach) * q)};
    const int32_t target[3] = {0, 0, (int32_t)llround((DS_KART_SPHERE + DS_CAMERA_TARGET_ELEVATION) * q)};
    mkds_property(m, "mksc:kart-scale", MKDS_PROPERTY_FIXED,
                  (uint32_t)mkds_round_div((int64_t)DS_KART_SPHERE * 65536 * 2, MKDS_UNITS_PER_METRE), 0);
    mkds_property_vector(m, "mksc:camera-eye", eye);
    mkds_property_vector(m, "mksc:camera-target", target);
    mkds_property(m, "mksc:camera-fov", MKDS_PROPERTY_INTEGER, 2u * DS_CAMERA_HALF_FOV, 0);
    return 4u;
}

/* Light and fog from NKM STAG and KartModelMain.carc/kartcrsmat.bin (one
 * 8-byte row per STAG course ID: diffuse, ambient, specular, emission).
 * ARM9 0x02074cd0 aims light 0 along the camera's view turned 0x2EE0/65536
 * turn about DS +Y (our -Z turn); karts take the STAG colour their KCL
 * prism selects and kartcrsmat's terms. Returns the META properties added. */
#define MKDS_CAMERA_LIGHT_TURN 12000
static uint32_t environment(Course *c, const NdsFs *rom, const MkdsStage *stage)
{
    RiCtx *ctx = c->m.ctx;
    const NdsFs karts = nds_narc(ctx, nds_file(ctx, rom, "data/KartModelMain.carc"), "data/KartModelMain.carc");
    const RiData table = nds_file(ctx, &karts, "kartcrsmat.bin");
    if (stage->course_id >= table.size / 8)
        ri_fail(ctx, "%s: STAG course %u has no kartcrsmat.bin row", c->m.where, (unsigned)stage->course_id);
    const uint32_t ambient = ri_get_u16(ctx, table, stage->course_id * 8 + 2) & 0x7FFFu,
                   emission = ri_get_u16(ctx, table, stage->course_id * 8 + 6) & 0x7FFFu;
    uint32_t count = 0;
    mkds_property(&c->m, "mksc:light-camera", MKDS_PROPERTY_INTEGER, (uint32_t)-MKDS_CAMERA_LIGHT_TURN, 0);
    mkds_property(&c->m, "mksc:model-ambient", MKDS_PROPERTY_INTEGER, mkds_rgb_word(ambient), 0);
    mkds_property(&c->m, "mksc:model-emission", MKDS_PROPERTY_INTEGER, mkds_rgb_word(emission), 0);
    count += 3;
    for (unsigned light = 0; light < 4; ++light)
    {
        char key[32];
        snprintf(key, sizeof(key), "mksc:light-color-%u", light);
        mkds_property(&c->m, key, MKDS_PROPERTY_INTEGER, mkds_rgb_word(stage->light_colors[light]), 0);
        ++count;
    }
    if (stage->fog_enabled)
    {
        mkds_property(&c->m, "mksc:fog", MKDS_PROPERTY_BOOLEAN, 1, 0);
        mkds_property(&c->m, "mksc:fog-curve", MKDS_PROPERTY_INTEGER, stage->fog_mode, 0);
        count += 2;
        c->m.fog = 1;
    }
    return count;
}

/* DS fog reads W depth: one fog unit is 1/8 view unit and the table's 32
 * steps cover 0x400 >> shift units each; a view unit is MKDS_MODEL_SCALE map
 * units. Returns Q16 metres. */
static uint32_t fog_metres(int64_t view_fx32)
{
    return (uint32_t)mkds_round_div(view_fx32 * MKDS_MODEL_SCALE * 65536, (int64_t)4096 * MKDS_UNITS_PER_METRE);
}

static void meta(Course *c, uint32_t bundle_property, uint32_t property_count, uint32_t preview, uint32_t minimap,
                 uint32_t laps, const MkdsStage *stage)
{
    const int64_t fog_start = stage->fog_distance > 0 ? stage->fog_distance : 0;
    const uint32_t fog_rgba = stage->fog_enabled ? mkds_rgb_word(stage->fog_color) : 0x5080B000u;
    const uint32_t fog_near = stage->fog_enabled ? fog_metres(fog_start) : 0,
                   fog_far = stage->fog_enabled ? fog_metres(fog_start + ((int64_t)4096 << (12 - stage->fog_shift)))
                                                : 512u << 16;
    const MkdsCourseSource *s = c->source;
    const uint32_t name = mkds_string(&c->m, s->name), author = mkds_string(&c->m, "Nintendo"), cup = mkds_string(&c->m, s->cup_name);
    const uint32_t modes = s->battle ? MODE_BATTLE : MODE_GP | MODE_TT | MODE_QUICK | MODE_VS;
    const uint32_t row[32] = {name, author, cup, modes, s->battle ? 0u : laps, s->battle ? MKDS_BATTLE_CUP_ID : MKDS_CUP_ID(s->cup),
                              s->slot, s->music ? 112u : MKDS_ABSENT, s->music ? s->music : MKDS_ABSENT, preview, minimap, MKDS_ABSENT, 0x5080B0FFu, fog_rgba, fog_near,
                              fog_far, 0x808080FFu, 0, 0, (uint32_t)-1073741824, 0xFFFFFFFFu,
                              (uint32_t)mkds_round_div((int64_t)281635 * 65536, 10000), MKDS_PHYSICS_ID, 0, bundle_property, property_count};
    mkds_words(&c->m, MKDS_T_META, row, 32);
}

void mkds_physics_section(RiCtx *ctx, uint32_t id)
{
    RiBuf *profile = ri_buf(ctx);
    for (size_t index = 0; index < sizeof(mkds_physics) / sizeof(mkds_physics[0]); ++index)
        ri_u32(ctx, profile, index == 3 ? id : mkds_physics[index]);
    ri_section(ctx, 102, id, PORTABLE_MESH_KART_RULES_VERSION, profile);
}

void mkds_course_convert(RiCtx *ctx, const NdsFs *rom, const MkdsCourseSource *source)
{
    static const int required[] = {MKDS_T_STRS, MKDS_T_META, MKDS_T_VERT, MKDS_T_VCOL, MKDS_T_TRIS,
                                   MKDS_T_MATS, MKDS_T_COLV, MKDS_T_COLT, MKDS_T_SURF, MKDS_T_CPNT,
                                   MKDS_T_CPED, MKDS_T_RTHD, MKDS_T_RTND, MKDS_T_RTED, MKDS_T_GRID};
    Course c;
    char where[64];
    snprintf(where, sizeof(where), "Mario Kart DS %s", source->folder);
    mkds_model_init(&c.m, ctx, where, MKDS_TEXTURE_ID(source->index, 0), required,
                    sizeof(required) / sizeof(required[0]), 256);
    c.source = source;
    c.platform_faces = ri_buf(ctx);
    c.platform_face_count = c.object_mesh_count = 0;
    c.solid_walls = ri_buf(ctx);
    c.solid_wall_count = 0;
    c.sway_link = 0;
    c.sway_lift = 0;
    char path[64], texture_path[64];
    snprintf(path, sizeof(path), "data/Course/%s.carc", source->folder);
    snprintf(texture_path, sizeof(texture_path), "data/Course/%sTex.carc", source->folder);
    char *archive_name = (char *)ri_buf_copy(ctx, path, strlen(path) + 1)->data;
    char *texture_name = (char *)ri_buf_copy(ctx, texture_path, strlen(texture_path) + 1)->data;
    const NdsFs archive = nds_narc(ctx, nds_file(ctx, rom, path), archive_name);
    RiData texture_file;
    NdsFs texture_archive = {0};
    const int has_texture_archive = nds_find(rom, texture_path, &texture_file);
    if (has_texture_archive)
        texture_archive = nds_narc(ctx, texture_file, texture_name);

    MkdsResources course, sky;
    if (!mkds_resources(&c.m, &archive, has_texture_archive ? &texture_archive : NULL, "course_model", &course))
        ri_fail(ctx, "%s: course_model.nsbmd is missing", c.m.where);
    const int has_sky =
        mkds_resources(&c.m, &archive, has_texture_archive ? &texture_archive : NULL, "course_model_V", &sky);
    const NitroModel *course_model = &course.models.models[0];
    const NitroModel *sky_model = has_sky ? &sky.models.models[0] : NULL;

    snprintf(path, sizeof(path), "%s/course_collision.kcl", archive_name);
    const MkdsKcl kcl = mkds_kcl(ctx, nds_file(ctx, &archive, "course_collision.kcl"),
                                 (char *)ri_buf_copy(ctx, path, strlen(path) + 1)->data);
    snprintf(path, sizeof(path), "%s/course_map.nkm", archive_name);
    const MkdsMap map = mkds_nkm(ctx, nds_file(ctx, &archive, "course_map.nkm"),
                                 (char *)ri_buf_copy(ctx, path, strlen(path) + 1)->data);

    mkds_string(&c.m, "");
    /* ARM9 0x02062078 draws course_model_V first, under the course's own
     * camera and identity model matrix (0x0217abac), with a farther far
     * plane: the sky is world-placed, not centred on the eye. */
    if (sky_model)
    {
        c.m.sky_property = mkds_property(&c.m, "mksc:sky", MKDS_PROPERTY_BOOLEAN, 1, 0);
        mkds_property(&c.m, "mksc:sky-world", MKDS_PROPERTY_BOOLEAN, 1, 0);
    }
    const uint32_t bundle_property = mkds_property(&c.m, "mksc:texture-bundle", MKDS_PROPERTY_ASSET,
                                                   MKDS_KIND_BUNDLE, MKDS_COURSE_ID(source->index));
    /* Minimap art and its world extent, then the cup screen's course
     * picture; META's property span covers the bundle and both bounds. */
    const NdsFs *map_archive = has_texture_archive ? &texture_archive : &archive;
    char map_where[160];
    snprintf(map_where, sizeof(map_where), "%s/Map2D/global1.NSCR", map_archive->name);
    const Nitro2DImage map_art =
        nitro_screen(ctx, nds_file(ctx, map_archive, "Map2D/global.NCLR"), nds_file(ctx, map_archive, "Map2D/global.NCGR"),
                     nds_file(ctx, map_archive, "Map2D/global1.NSCR"), map_where);
    int32_t low[3], high[3];
    const uint32_t minimap_image = minimap(&c, &map, &map_art, low, high);
    mkds_property_vector(&c.m, "mksc:minimap-low", low);
    mkds_property_vector(&c.m, "mksc:minimap-high", high);
    const char *missing = incomplete(&c, &map);
    uint32_t meta_properties = 3u;
    if (missing[0] && ++meta_properties)
        mkds_property(&c.m, "mksc:incomplete", MKDS_PROPERTY_STRING, mkds_string(&c.m, missing), 0);
    /* Courses race with the DS Grand Prix odds, VS with its own, and battle
     * stages with the balloon battle odds (mkds_rules.c). */
    ++meta_properties;
    mkds_property(&c.m, "mksc:item-rules", MKDS_PROPERTY_ASSET, PORTABLE_MESH_ITEM_RULES_KIND,
                  source->battle ? MKDS_ITEMS_BATTLE_ID : MKDS_ITEMS_ID);
    if (!source->battle && ++meta_properties)
        mkds_property(&c.m, "mksc:item-rules-vs", MKDS_PROPERTY_ASSET, PORTABLE_MESH_ITEM_RULES_KIND,
                      MKDS_ITEMS_VS_ID);
    /* ...and with the DS sounds (mkds_sound_set). */
    ++meta_properties;
    mkds_property(&c.m, "mksc:sound-set", MKDS_PROPERTY_ASSET, MKDS_SOUND_SET_KIND, MKDS_SOUND_SET_ID);
    /* ...and with the DS shadows and particle art (mkds_fx.c). */
    ++meta_properties;
    mkds_property(&c.m, "mksc:fx-set", MKDS_PROPERTY_ASSET, MKDS_FX_SET_KIND, MKDS_FX_SET_ID);
    /* ...and at the DS's kart size and race camera (chase_properties). */
    meta_properties += chase_properties(&c.m);
    meta_properties += environment(&c, rom, &map.stage);
    uint32_t preview = MKDS_ABSENT;
    if (!source->battle)
    {
        char stem[64], files[3][96];
        snprintf(stem, sizeof(stem), "data/CupPicture/select_cup_%s%02u_m", source->cup < 4 ? "nitro" : "retro",
                 (unsigned)((source->cup % 4) * 4 + source->slot + 1));
        snprintf(files[0], sizeof(files[0]), "%s_b.NCLR", stem);
        snprintf(files[1], sizeof(files[1]), "%s_b.NCGR", stem);
        snprintf(files[2], sizeof(files[2]), "%s_picture.NSCR", stem);
        preview = screen_image(&c, rom, files[0], files[1], files[2]);
    }
    mkds_materials(&c.m, course_model, &course.textures, course.has_srt ? &course.srt : NULL,
                   course.has_pattern ? &course.pattern : NULL, 0);
    if (sky_model)
        mkds_materials(&c.m, sky_model, &sky.textures, sky.has_srt ? &sky.srt : NULL,
                       sky.has_pattern ? &sky.pattern : NULL, 1);
    mkds_geometry(&c.m, course_model, 0, 0);
    if (sky_model)
        mkds_geometry(&c.m, sky_model, course_model->material_count, 1);
    if (c.m.tables[MKDS_T_TRIS].count == 0)
        ri_fail(ctx, "%s: course model has no drawable triangles", c.m.where);
    scenery(&c, &archive, has_texture_archive ? &texture_archive : NULL, &map);
    sound_zones(&c, &map);
    gameplay(&c, &kcl, &map);
    intro(&c, &map);
    meta(&c, bundle_property, meta_properties, preview, minimap_image, map.laps, &map.stage);

    const uint32_t id = MKDS_COURSE_ID(source->index);
    RiBuf *catalog = ri_buf(ctx);
    ri_put(ctx, catalog, "C3C1", 4);
    ri_words(ctx, catalog, 9, 1u, 1u, 24u, id, source->battle ? MKDS_BATTLE_CUP_ID : MKDS_CUP_ID(source->cup),
             source->slot, source->battle ? (uint32_t)MODE_BATTLE : (uint32_t)(MODE_GP | MODE_TT | MODE_QUICK | MODE_VS),
             source->index, 0u);
    RiBuf *blob = mkds_model_blob(&c.m, "C3D1", id);
    RiBuf *textures = mkds_model_bundle(&c.m);
    ri_section(ctx, KIND_COURSE, id, 1, blob);
    ri_section(ctx, KIND_CATALOG, id, 1, catalog);
    ri_section(ctx, MKDS_KIND_BUNDLE, id, 1, textures);
}
