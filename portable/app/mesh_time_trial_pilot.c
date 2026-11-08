#include "mesh_time_trial_pilot.h"
#include "fixed_3d.h"
#include "mesh_kart_mk8.h"
#include "mesh_query.h"
#include <stdio.h>
#include <string.h>
#define Q PORTABLE_FIXED_3D_ONE
#define U PORTABLE_FIXED_3D_UNIT
enum { FINISH_GATE_LIMIT = 1024, FINISH_FACE_LIMIT = 512 };
#define FINISH_RANGE (40 * Q)
/* Finish publishes after a complete move. Eight metres also clears one full
 * bounded XY step (both velocity components<=256m/s), not the item duration. */
#define FINISH_EXIT_MARGIN (8 * Q)
/* Ten degrees is an aid admission bound, not a physics/handling constant. */
#define FINISH_DIRECTION_COS INT32_C(1057429273)
/* Ground alignment eases up toward the floor normal in truncating steps, so a
 * kart leaving a slope keeps a residue of a few Q30 units; level means within
 * about a twentieth of a degree. */
#define LEVEL_TOLERANCE (U / 1024)
static int level_kart(const PortableMeshKartState *kart)
{
    return kart->up_q30[0] >= -LEVEL_TOLERANCE && kart->up_q30[0] <= LEVEL_TOLERANCE &&
           kart->up_q30[1] >= -LEVEL_TOLERANCE && kart->up_q30[1] <= LEVEL_TOLERANCE &&
           kart->up_q30[2] >= U - LEVEL_TOLERANCE &&
           kart->forward_q30[2] >= -LEVEL_TOLERANCE && kart->forward_q30[2] <= LEVEL_TOLERANCE;
}
uint8_t portable_mesh_time_trial_pilot_defensive_hold(const PortableMeshItems *world,
    const PortableMeshRacerState *racer, const PortableMeshItemRules *rules,
    unsigned player, PortableMeshKartInput driving, uint8_t periodic_pressed)
{
    if (!world || !racer || !portable_mesh_item_rules_valid(rules) ||
        !world->inventory.count || world->inventory.count > 8u ||
        player >= world->inventory.count || periodic_pressed > 1u ||
        racer->phase != PORTABLE_MESH_RACER_RACING || racer->kart.respawn_ticks ||
        racer->respawn_grace || (driving.buttons & PORTABLE_MESH_RESCUE))
        return 0;
    int held = 0;
    for (unsigned i = 0; i < PORTABLE_MESH_ITEMS_ACTORS; ++i)
    {
        const PortableMeshItemActor *actor = &world->actors[i];
        if (actor->owner == player && actor->phase == PORTABLE_MESH_ACTOR_HELD &&
            actor->item == 0x1000u && actor->serial)
            held = 1;
    }
    if (!held || periodic_pressed)
        return periodic_pressed;
    const int64_t range = rules->cpu_use_distance_q16;
    for (unsigned i = 0; i < PORTABLE_MESH_ITEMS_ACTORS; ++i)
    {
        const PortableMeshItemActor *actor = &world->actors[i];
        if (actor->owner == player || actor->owner >= world->inventory.count ||
            actor->phase != PORTABLE_MESH_ACTOR_FLYING || !actor->serial ||
            (actor->item != 0x1000u && actor->item != 0x1002u))
            continue;
        uint64_t square = 0;
        int64_t closing = 0;
        int valid = 1;
        for (unsigned axis = 0; axis < 3u; ++axis)
        {
            const int32_t position = actor->motion.position_q16[axis];
            const int32_t velocity = actor->motion.velocity_q16[axis];
            if (position < -PORTABLE_FIXED_3D_POSITION_LIMIT || position > PORTABLE_FIXED_3D_POSITION_LIMIT ||
                racer->kart.position_q16[axis] < -PORTABLE_FIXED_3D_POSITION_LIMIT ||
                racer->kart.position_q16[axis] > PORTABLE_FIXED_3D_POSITION_LIMIT ||
                velocity < -256*Q || velocity > 256*Q ||
                racer->kart.velocity_q16[axis] < -256*Q || racer->kart.velocity_q16[axis] > 256*Q)
            { valid = 0; break; }
            const int64_t relative = (int64_t)position-racer->kart.position_q16[axis];
            const int64_t relative_velocity = (int64_t)velocity-racer->kart.velocity_q16[axis];
            square += (uint64_t)(relative*relative);
            closing += relative*relative_velocity;
        }
        if (valid && square <= (uint64_t)(range*range) &&
            (closing < 0 || (actor->item == 0x1002u && actor->target == player)))
            return 1;
    }
    return periodic_pressed;
}

uint8_t portable_mesh_time_trial_pilot_item_press(const PortableMeshTimeTrialSession *session,
    const PortableMeshTimeTrialPilotState *pilot, const PortableMeshItemRacer *inventory,
    const PortableMeshItemRacerStatus *status, PortableMeshKartInput driving, uint8_t periodic_pressed)
{
    if (session==NULL || pilot==NULL || inventory==NULL || status==NULL) return 0;
    if (!pilot->active) return periodic_pressed; /* Legacy G retains its original pulse. */
    const uint16_t item=inventory->held_item;
    if (item!=0x1007u && item!=0x1008u && item!=0x1009u && item!=0x100fu && item!=0x100au)
        return periodic_pressed;
    if (session==NULL || status==NULL || inventory->active || inventory->previous_item_pressed ||
        status->pending_mushroom || status->mushroom_ticks || status->golden_ticks || status->star_ticks ||
        status->spin_ticks || !(driving.buttons&PORTABLE_MESH_ACCELERATE) ||
        (driving.buttons&(PORTABLE_MESH_BRAKE|PORTABLE_MESH_HOP|PORTABLE_MESH_RESCUE))) return 0;
    const PortableMeshKartState *kart=&session->state.kart;
    if (kart->panel_ticks || kart->boost_ticks[0] || kart->boost_ticks[1] || kart->boost_ticks[2]) return 0;
    PortableMeshFinishWindow window;
    return (uint8_t)portable_mesh_time_trial_pilot_finish_window(session,pilot,&window);
}
typedef struct { uint64_t numerator, denominator; } FinishRatio;
typedef struct { FinishRatio first, last; } FinishInterval;
/* Continued-fraction comparison avoids cross products of two 60-bit plane
 * numerators. Denominators are positive and all fractions are in[0,1]. */
static int finish_ratio_compare(FinishRatio a, FinishRatio b)
{
    int reverse = 0;
    for (;;)
    {
        const uint64_t qa = a.numerator / a.denominator, qb = b.numerator / b.denominator;
        if (qa != qb)
            return (qa < qb ? -1 : 1) * (reverse ? -1 : 1);
        const uint64_t ra = a.numerator % a.denominator, rb = b.numerator % b.denominator;
        if (!ra || !rb)
            return (ra == rb ? 0 : !ra ? -1 : 1) * (reverse ? -1 : 1);
        a = (FinishRatio){a.denominator, ra};
        b = (FinishRatio){b.denominator, rb};
        reverse = !reverse;
    }
}
static int finish_triangle_interval(const PortableMeshTriangle *triangle,
    const int32_t before[3], const int32_t after[3], FinishInterval *out)
{
    FinishInterval interval = {{0,1},{1,1}};
    for (unsigned i=0; i<3; ++i)
    {
        const int32_t *a=triangle->vertices_q16[i], *b=triangle->vertices_q16[(i+1u)%3u];
        const int64_t dx=(int64_t)b[0]-a[0], dy=(int64_t)b[1]-a[1];
        const int64_t start=dx*((int64_t)before[1]-a[1])-dy*((int64_t)before[0]-a[0]);
        const int64_t end=dx*((int64_t)after[1]-a[1])-dy*((int64_t)after[0]-a[0]);
        if (start<0 && end<0) return 0;
        if (start<0)
        {
            const FinishRatio entry={(uint64_t)-start,(uint64_t)(end-start)};
            if (finish_ratio_compare(entry,interval.first)>0) interval.first=entry;
        }
        else if (end<0)
        {
            const FinishRatio exit={(uint64_t)start,(uint64_t)(start-end)};
            if (finish_ratio_compare(exit,interval.last)<0) interval.last=exit;
        }
    }
    if (finish_ratio_compare(interval.first,interval.last)>0) return 0;
    *out=interval;
    return 1;
}
static int finish_clear_line(const PortableMeshTimeTrialSession *r,
    const int32_t end[3], int32_t floor_z)
{
    const PortableMeshCourse *geometry=r->geometry;
    const int32_t radius=r->rules->kart_radius_q16;
    if (!geometry || !geometry->next_triangle || !geometry->triangle || !geometry->surface ||
        !geometry->triangle_count || geometry->triangle_count>262144u)
        return 0;
    int32_t low[3],high[3];
    if (!portable_mesh_query_bounds(r->state.kart.position_q16,end,radius,low,high)) return 0;
    low[2]=floor_z;
    FinishInterval intervals[FINISH_FACE_LIMIT];
    unsigned count=0, faces=0;
    uint32_t first=0;
    for (;;)
    {
        uint32_t index;
        const PortableMeshQueryStatus query=portable_mesh_query_next_triangle(geometry,low,high,first,&index);
        if (query==PORTABLE_MESH_QUERY_MISS) break;
        if (query!=PORTABLE_MESH_QUERY_HIT || ++faces>FINISH_FACE_LIMIT) return 0;
        first=index+1u;
        PortableMeshTriangle triangle;
        PortableMeshSurface surface;
        if (!geometry->triangle(geometry->context,index,&triangle) ||
            !geometry->surface(geometry->context,triangle.surface,&surface)) return 0;
        int overlaps=1;
        for (unsigned axis=0;axis<3;++axis)
        {
            int32_t minimum=INT32_MAX,maximum=INT32_MIN;
            for (unsigned i=0;i<3;++i)
            {
                const int32_t value=triangle.vertices_q16[i][axis];
                if (value < -PORTABLE_FIXED_3D_POSITION_LIMIT || value>PORTABLE_FIXED_3D_POSITION_LIMIT)
                    return 0;
                if (value<minimum) minimum=value;
                if (value>maximum) maximum=value;
            }
            if (maximum<low[axis] || minimum>high[axis]) overlaps=0;
        }
        if (!overlaps) continue;
        int32_t normal[3];
        if (!portable_mesh_query_normal(&triangle,normal) || normal[0] || normal[1] || normal[2]!=U ||
            triangle.vertices_q16[0][2]!=floor_z || triangle.vertices_q16[1][2]!=floor_z ||
            triangle.vertices_q16[2][2]!=floor_z || surface.type>2u)
            return 0; /* Conservative: even an AABB-only obstacle/hazard overlap refuses. */
        FinishInterval interval;
        if (finish_triangle_interval(&triangle,r->state.kart.position_q16,end,&interval))
            intervals[count++]=interval;
    }
    FinishRatio covered={0,1};
    /* Exact rational union retains shared triangle edges with no epsilon that
     * could silently bridge an uncovered floor or a thin recovery strip. */
    for (unsigned pass=0;pass<count;++pass)
    {
        int changed=0;
        for (unsigned i=0;i<count;++i)
            if (finish_ratio_compare(intervals[i].first,covered)<=0 &&
                finish_ratio_compare(intervals[i].last,covered)>0)
            { covered=intervals[i].last; changed=1; }
        if (covered.numerator==covered.denominator) return 1;
        if (!changed) break;
    }
    return 0;
}
static int finish_route_clear(const PortableMeshTimeTrialSession *r,
    const PortableMeshTimeTrialPilotState *pilot, const PortableCourse3DCheckpoint *finish,
    int32_t floor_z)
{
    PortableCourse3DRoute route;
    if (!portable_course_3d_route(r->course,pilot->cpu.route,&route) || route.kind || route.flags!=1u ||
        !route.node_count || route.node_count>FINISH_GATE_LIMIT ||
        route.node_first>UINT32_MAX-route.node_count ||
        pilot->cpu.from_node<route.node_first || pilot->cpu.from_node-route.node_first>=route.node_count)
        return 0;
    PortableCourse3DProperty enabled;
    if (!portable_course_3d_property_find(r->course,route.property_first,route.property_count,
        "mksc:route-pilot-continuous",&enabled) || enabled.type!=PORTABLE_COURSE_3D_PROPERTY_BOOLEAN ||
        !enabled.value.boolean) return 0;
    uint32_t from=pilot->cpu.from_node;
    int32_t previous[3];memcpy(previous,r->state.kart.position_q16,sizeof previous);
    previous[2]=floor_z;
    uint64_t distance=0;
    for (uint32_t walked=0;walked<route.node_count;++walked)
    {
        PortableCourse3DRouteNode a,b;
        uint32_t to;
        if (!portable_course_3d_route_node(r->course,from,&a) || a.edge_count!=1u ||
            !portable_course_3d_route_edge(r->course,a.edge_first,&to) ||
            to<route.node_first || to-route.node_first>=route.node_count ||
            !portable_course_3d_route_node(r->course,to,&b) ||
            (walked==0 && (to!=pilot->cpu.to_node || a.edge_first!=pilot->cpu.selected_edge))) return 0;
        int64_t raw[3];uint64_t square=0;
        for (unsigned axis=0;axis<3;++axis)
        {
            if (b.position_q16[axis] < -PORTABLE_FIXED_3D_POSITION_LIMIT ||
                b.position_q16[axis]>PORTABLE_FIXED_3D_POSITION_LIMIT) return 0;
            raw[axis]=(int64_t)b.position_q16[axis]-previous[axis];
            square+=(uint64_t)(raw[axis]*raw[axis]);
        }
        if (b.position_q16[2]!=floor_z) return 0;
        int32_t direction[3];
        if (!portable_fixed_3d_normalize(raw,direction) ||
            portable_fixed_3d_dot(direction,r->state.kart.forward_q30)/U < FINISH_DIRECTION_COS)
            return 0;
        int64_t time;
        if (portable_mesh_racer_gate_crossing(finish,previous,b.position_q16,&time)) return 1;
        distance+=portable_fixed_3d_sqrt(square);
        if (distance>FINISH_RANGE) return 0;
        memcpy(previous,b.position_q16,sizeof previous);
        from=to;
    }
    return 0;
}
int portable_mesh_time_trial_pilot_finish_window(const PortableMeshTimeTrialSession *r,
    const PortableMeshTimeTrialPilotState *pilot, PortableMeshFinishWindow *out)
{
    if (!r || !pilot || !out || !r->course || r->course->table_count>32u || !pilot->active ||
        pilot->active>1u || !portable_mesh_kart_rules_valid(r->rules) ||
        !portable_mesh_kart_state_valid(&r->state.kart) ||
        (r->mode!=1u && r->mode!=4u && r->mode!=8u) || pilot->mode!=r->mode ||
        pilot->cpu.course_id!=r->course->id || pilot->cpu.profile_id!=r->rules->profile_id ||
        pilot->cpu.profile_revision!=r->rules->revision ||
        r->state.phase!=PORTABLE_MESH_RACER_RACING || r->state.crossing_armed!=1u ||
        r->state.respawn_grace || r->state.kart.respawn_ticks || !r->state.kart.grounded ||
        !level_kart(&r->state.kart)) return 0;
    PortableCourse3DMeta meta;
    if (!portable_course_3d_meta(r->course,&meta) || !meta.laps || r->state.lap!=meta.laps-1u) return 0;
    const PortableCourse3DTable *gates=portable_course_3d_table(r->course,
        PORTABLE_COURSE_3D_TAG('C','P','N','T'));
    if (!gates || !gates->count || gates->count>FINISH_GATE_LIMIT) return 0;
    int32_t end[3];
    for (unsigned i=0;i<3;++i)
    {
        const int64_t value=(int64_t)r->state.kart.position_q16[i]+
            (int64_t)r->state.kart.forward_q30[i]*FINISH_RANGE/U;
        if (value < -PORTABLE_FIXED_3D_POSITION_LIMIT || value>PORTABLE_FIXED_3D_POSITION_LIMIT) return 0;
        end[i]=(int32_t)value;
    }
    uint8_t visited[FINISH_GATE_LIMIT]={0};
    uint32_t current=r->state.checkpoint;
    int64_t last=-1;
    const int32_t radius=r->rules->kart_radius_q16;
    for (uint32_t walked=0;walked<gates->count;++walked)
    {
        PortableCourse3DCheckpoint previous,chosen={0};
        uint32_t target=PORTABLE_MESH_ABSENT;
        int64_t best=INT64_MAX;
        if (!portable_course_3d_checkpoint(r->course,current,&previous) ||
            !previous.edge_count || previous.edge_count>gates->count ||
            previous.edge_first>UINT32_MAX-previous.edge_count) return 0;
        for (uint32_t edge=0;edge<previous.edge_count;++edge)
        {
            uint32_t index;PortableCourse3DCheckpoint gate;int64_t time;
            if (!portable_course_3d_checkpoint_edge(r->course,previous.edge_first+edge,&index) ||
                index>=gates->count || !portable_course_3d_checkpoint(r->course,index,&gate)) return 0;
            if (gate.half_width_q16<=radius || gate.half_height_q16<=radius) continue;
            gate.half_width_q16-=radius;gate.half_height_q16-=radius;
            gate.flags|=4u; /* Aid admission is always forward-only. */
            if (!visited[index] && portable_mesh_racer_gate_crossing(&gate,r->state.kart.position_q16,end,&time) &&
                time>=last && time<best) {target=index;best=time;chosen=gate;}
        }
        if (target==PORTABLE_MESH_ABSENT) return 0;
        visited[target]=1;last=best;
        if (chosen.flags&1u)
        {
            PortableMeshFinishWindow window={0};
            for (unsigned axis=0;axis<3;++axis)
                window.finish_position_q16[axis]=r->state.kart.position_q16[axis]+(int32_t)(
                    ((int64_t)end[axis]-r->state.kart.position_q16[axis])*best/U);
            window.distance_q16=(int32_t)((int64_t)FINISH_RANGE*best/U);
            window.gate_count=walked+1u;
            int32_t clear_end[3];
            for (unsigned axis=0;axis<3;++axis)
            {
                const int64_t value=(int64_t)window.finish_position_q16[axis]+
                    (int64_t)r->state.kart.forward_q30[axis]*FINISH_EXIT_MARGIN/U;
                if (value < -PORTABLE_FIXED_3D_POSITION_LIMIT || value>PORTABLE_FIXED_3D_POSITION_LIMIT)
                    return 0;
                clear_end[axis]=(int32_t)value;
            }
            const int64_t floor=(int64_t)r->state.kart.position_q16[2]-radius-r->rules->ride_height_q16;
            if (floor < -PORTABLE_FIXED_3D_POSITION_LIMIT || floor>PORTABLE_FIXED_3D_POSITION_LIMIT ||
                !finish_route_clear(r,pilot,&chosen,(int32_t)floor) ||
                !finish_clear_line(r,clear_end,(int32_t)floor)) return 0;
            *out=window;
            return 1;
        }
        current=target;
    }
    return 0;
}

static uint32_t word(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) | ((uint32_t)p[2] << 16u) |
           ((uint32_t)p[3] << 24u);
}
static int32_t signed_word(const uint8_t *p)
{
    const uint32_t n = word(p);
    return n <= INT32_MAX ? (int32_t)n : -1 - (int32_t)(UINT32_MAX - n);
}
static const uint8_t *row(const PortableCourse3DTable *table, uint32_t index)
{
    return table && index < table->count ? table->bytes + (size_t)index * table->stride : NULL;
}
static int budget_table(const PortableCourse3DTable *table, uint32_t stride, uint32_t cap)
{
    return table && table->bytes && table->stride == stride && table->count &&
           table->count <= cap;
}
uint32_t portable_mesh_time_trial_pilot_tick_budget(const PortableCourse3DView *course)
{
    const uint32_t minimum = 12000u, maximum = 180000u;
    if (!course || course->table_count > 32u)
        return minimum;
    const PortableCourse3DTable *meta = portable_course_3d_table(course,
        PORTABLE_COURSE_3D_TAG('M','E','T','A'));
    const PortableCourse3DTable *headers = portable_course_3d_table(course,
        PORTABLE_COURSE_3D_TAG('R','T','H','D'));
    const PortableCourse3DTable *nodes = portable_course_3d_table(course,
        PORTABLE_COURSE_3D_TAG('R','T','N','D'));
    const PortableCourse3DTable *edges = portable_course_3d_table(course,
        PORTABLE_COURSE_3D_TAG('R','T','E','D'));
    if (!budget_table(meta,128u,1u) || !budget_table(headers,32u,16384u) ||
        !budget_table(nodes,32u,16384u) || !budget_table(edges,4u,16384u))
        return minimum;
    const uint32_t modes = word(meta->bytes + 12u), laps = word(meta->bytes + 16u);
    if (!(modes & 15u) || !laps || laps > 255u)
        return minimum;
    const uint8_t *route = NULL;
    for (uint32_t i=0;i<headers->count;++i)
    {
        const uint8_t *candidate = row(headers,i);
        if (word(candidate)==0u && word(candidate+16u)==1u)
        {
            route=candidate;
            break;
        }
    }
    if (!route)
        return minimum;
    const uint32_t first=word(route+4u), count=word(route+8u), start=word(route+12u);
    if (!count || first>nodes->count || count>nodes->count-first ||
        start<first || start-first>=count || word(route+28u))
        return minimum;
    uint64_t length=0;
    for (uint32_t i=first;i<first+count;++i)
    {
        const uint8_t *a=row(nodes,i);
        const uint32_t edge=word(a+20u), successors=word(a+24u);
        if (!successors || edge>edges->count || successors>edges->count-edge || word(a+28u))
            return minimum;
        const uint32_t target=word(row(edges,edge));
        if (target<first || target-first>=count)
            return minimum;
        const uint8_t *b=row(nodes,target);
        uint64_t square=0;
        for (unsigned axis=0;axis<3u;++axis)
        {
            const int32_t x=signed_word(a+4u*axis), y=signed_word(b+4u*axis);
            if (x < -PORTABLE_FIXED_3D_POSITION_LIMIT || x > PORTABLE_FIXED_3D_POSITION_LIMIT ||
                y < -PORTABLE_FIXED_3D_POSITION_LIMIT || y > PORTABLE_FIXED_3D_POSITION_LIMIT)
                return minimum;
            const int64_t delta=(int64_t)y-x;
            square+=(uint64_t)(delta*delta);
        }
        length+=portable_fixed_3d_sqrt(square);
    }
    /* At most 16384 bounded segments *255 laps *20 ticks/metre fits u64.
     * Round the fractional tick up; never let short-course aids shrink. */
    const uint64_t ticks=(length*laps*20u+Q-1u)/Q+600u;
    return ticks<minimum ? minimum : ticks>maximum ? maximum : (uint32_t)ticks;
}
static int32_t fraction(int64_t numerator, int64_t denominator)
{
    if (numerator <= 0)
        return 0;
    if (numerator >= denominator)
        return Q;
    uint64_t n = (uint64_t)numerator;
    const uint64_t d = (uint64_t)denominator;
    uint32_t result = 0;
    for (unsigned i = 0; i < 16u; ++i)
    {
        n *= 2u;
        result *= 2u;
        if (n >= d)
        {
            n -= d;
            ++result;
        }
    }
    return (int32_t)result;
}
static PortableMeshStatus unavailable(const PortableMeshTimeTrialSession *session, char *error,
                                      size_t capacity)
{
    if (error && capacity)
        snprintf(error, capacity, "3D TT course=%u: route aid requires a connected CPU route",
                 session && session->course ? (unsigned)session->course->id : 0u);
    return PORTABLE_MESH_UNSUPPORTED;
}
static PortableMeshStatus planner_invalid(const PortableMeshTimeTrialSession *session,
                                           char *error, size_t capacity)
{
    if (error && capacity)
        snprintf(error,capacity,"3D route pilot course=%u tick=%u: invalid continuous route property/state",
                 session && session->course ? session->course->id : 0u,
                 session ? session->state.tick : 0u);
    return PORTABLE_MESH_INVALID;
}
/* Aid-only nominal traversal estimate, not a second driving/simulation path.
 * Sixteen midpoint samples per authored edge integrate its speed cap and the
 * production CPU's future-corner braking envelope. Acceleration, collisions,
 * items and terrain are deliberately not predicted. All candidates share one
 * bounded operation budget; ties retain the first canonical route index. */
#define PILOT_COST_SAMPLES 16u
#define PILOT_COST_WORK 4194304u
static int cost_span(const PortableCourse3DView *course, const PortableCourse3DRoute *route,
    uint32_t from, PortableCourse3DRouteNode *b, uint32_t *to, int32_t direction[3],
    uint64_t *length, uint32_t *work)
{
    PortableCourse3DRouteNode a;
    if (!*work)
        return 0;
    --*work;
    if (from < route->node_first || from-route->node_first >= route->node_count ||
        !portable_course_3d_route_node(course,from,&a) || a.edge_count != 1u ||
        !portable_course_3d_route_edge(course,a.edge_first,to) ||
        *to < route->node_first || *to-route->node_first >= route->node_count ||
        !portable_course_3d_route_node(course,*to,b))
        return 0;
    int64_t delta[3];
    uint64_t square=0;
    for (unsigned axis=0;axis<3u;++axis)
    {
        if (a.position_q16[axis] < -PORTABLE_FIXED_3D_POSITION_LIMIT ||
            a.position_q16[axis] > PORTABLE_FIXED_3D_POSITION_LIMIT ||
            b->position_q16[axis] < -PORTABLE_FIXED_3D_POSITION_LIMIT ||
            b->position_q16[axis] > PORTABLE_FIXED_3D_POSITION_LIMIT)
            return 0;
        delta[axis]=(int64_t)b->position_q16[axis]-a.position_q16[axis];
        square+=(uint64_t)(delta[axis]*delta[axis]);
    }
    *length=portable_fixed_3d_sqrt(square);
    return *length && portable_fixed_3d_normalize(delta,direction);
}
static int32_t cost_speed(const PortableCourse3DRouteNode *node,
                           const PortableMeshKartRules *rules)
{
    return node->speed_q16 > 0 && node->speed_q16 < rules->forward_speed_q16 ?
        node->speed_q16 : rules->forward_speed_q16;
}
static int route_cost(const PortableMeshTimeTrialSession *session,
    const PortableCourse3DRoute *route, const PortableMeshCpuState *cpu,
    uint64_t *cost, uint32_t *work)
{
    const int64_t rate=session->rules->brake_deceleration_q16 >
        session->rules->coast_deceleration_q16 ? session->rules->brake_deceleration_q16 :
        session->rules->coast_deceleration_q16;
    if (rate <= 0)
        return 0;
    uint64_t total=0;
    uint32_t from=route->start_node;
    for (uint32_t edge=0;edge<route->node_count;++edge)
    {
        PortableCourse3DRouteNode b;
        uint32_t to;
        int32_t direction[3];
        uint64_t length;
        if (!cost_span(session->course,route,from,&b,&to,direction,&length,work))
            return 0;
        const int32_t cap=cost_speed(&b,session->rules);
        if (cap <= 0)
            return 0;
        for (unsigned sample=0;sample<PILOT_COST_SAMPLES;++sample)
        {
            int32_t speed=cap, previous_direction[3];
            memcpy(previous_direction,direction,sizeof direction);
            uint64_t distance=length*(2u*PILOT_COST_SAMPLES-2u*sample-1u)/
                              (2u*PILOT_COST_SAMPLES);
            const uint64_t horizon=(uint64_t)((int64_t)cap*cap/(2*rate))+
                                   (uint32_t)cpu->lookahead_q16;
            uint32_t cursor=to;
            for (uint32_t walked=0;walked<route->node_count && distance<=horizon;++walked)
            {
                PortableCourse3DRouteNode next;
                uint32_t target;
                int32_t following[3];
                uint64_t span;
                if (!cost_span(session->course,route,cursor,&next,&target,following,&span,work))
                    return 0;
                int32_t limit=cost_speed(&next,session->rules);
                if (portable_fixed_3d_dot(previous_direction,following)/U < 966367642 &&
                    limit > cpu->corner_speed_q16)
                    limit=cpu->corner_speed_q16;
                const uint64_t available=distance>(uint32_t)cpu->lookahead_q16 ?
                    distance-(uint32_t)cpu->lookahead_q16 : 0;
                /* distance<=cap²/(2a)+lookahead bounds this product; bounded
                 * rules/positions keep squares and the sum below 2^63. */
                const uint64_t allowed=portable_fixed_3d_sqrt(
                    (uint64_t)((int64_t)limit*limit)+2u*(uint64_t)rate*available);
                if (allowed<(uint32_t)speed)
                    speed=(int32_t)allowed;
                distance+=span;
                cursor=target;
                memcpy(previous_direction,following,sizeof following);
            }
            if (speed <= 0)
                return 0;
            const uint64_t denominator=PILOT_COST_SAMPLES*(uint64_t)(uint32_t)speed;
            const uint64_t numerator=length*Q;
            const uint64_t term=(numerator+denominator-1u)/denominator;
            if (UINT64_MAX-total<term)
                return 0;
            total+=term;
        }
        from=to;
        if ((from==route->start_node)!=(edge+1u==route->node_count))
            return 0; /* Exactly one closed simple cycle, with no hidden nodes. */
    }
    *cost=total;
    return 1;
}
/* Validate every node/edge before production acquisition arithmetic. A valid
 * branching graph or several cycles is supported by the controller but has no
 * unique lap to price; classify it separately instead of rejecting content. */
static int cost_preflight(const PortableCourse3DView *course,
    const PortableCourse3DRoute *route, int *simple, uint32_t *work)
{
    if (!route->node_count || route->node_count>16384u ||
        route->node_first>UINT32_MAX-route->node_count ||
        route->start_node<route->node_first ||
        route->start_node-route->node_first>=route->node_count)
        return 0;
    *simple=1;
    for (uint32_t i=0;i<route->node_count;++i)
    {
        PortableCourse3DRouteNode a;
        if (!*work)
            return 0;
        --*work;
        if (!portable_course_3d_route_node(course,route->node_first+i,&a) ||
            !a.edge_count || a.edge_count>16384u ||
            a.edge_first>UINT32_MAX-a.edge_count)
            return 0;
        for (unsigned axis=0;axis<3u;++axis)
            if (a.position_q16[axis]<-PORTABLE_FIXED_3D_POSITION_LIMIT ||
                a.position_q16[axis]>PORTABLE_FIXED_3D_POSITION_LIMIT)
                return 0;
        if (a.edge_count!=1u)
            *simple=0;
        for (uint32_t edge=0;edge<a.edge_count;++edge)
        {
            uint32_t to;
            if (!*work)
                return 0;
            --*work;
            if (!portable_course_3d_route_edge(course,a.edge_first+edge,&to) ||
                to<route->node_first || to-route->node_first>=route->node_count)
                return 0;
        }
    }
    if (*simple)
    {
        uint32_t from=route->start_node;
        for (uint32_t i=0;i<route->node_count;++i)
        {
            PortableCourse3DRouteNode node;
            uint32_t to;
            if (!*work)
                return 0;
            --*work;
            if (!portable_course_3d_route_node(course,from,&node) ||
                !portable_course_3d_route_edge(course,node.edge_first,&to))
                return 0;
            from=to;
            if ((from==route->start_node)!=(i+1u==route->node_count))
                *simple=0;
        }
    }
    return 1;
}
static PortableMeshStatus nominal_route(PortableMeshCpuState *out,
    const PortableMeshTimeTrialSession *session, char *error, size_t capacity)
{
    const PortableCourse3DTable *headers=portable_course_3d_table(session->course,
        PORTABLE_COURSE_3D_TAG('R','T','H','D'));
    uint64_t best=UINT64_MAX;
    uint32_t work=PILOT_COST_WORK;
    PortableMeshCpuState selected={0};
    if (!headers || headers->count>16384u)
        return planner_invalid(session,error,capacity);
    uint32_t eligible=0, only=PORTABLE_MESH_ABSENT;
    for (uint32_t index=0;index<headers->count;++index)
    {
        PortableCourse3DRoute route;
        if (!portable_course_3d_route(session->course,index,&route))
            return planner_invalid(session,error,capacity);
        if (route.kind==0u && route.flags==1u)
        {
            ++eligible;
            only=index;
        }
    }
    /* There is no ranking decision with one candidate: preserve every closed
     * graph the existing controller supports, including authored branches. */
    if (eligible==1u)
        return portable_mesh_cpu_init(out,session,only,error,capacity);
    int unrankable=0;
    /* Complete validation precedes either ranking or legacy fallback, so a
     * malformed later candidate cannot be hidden by a valid branching one. */
    for (uint32_t index=0;index<headers->count;++index)
    {
        PortableCourse3DRoute route;
        if (!portable_course_3d_route(session->course,index,&route))
            return planner_invalid(session,error,capacity);
        if (route.kind!=0u || route.flags!=1u)
            continue;
        int simple;
        if (!cost_preflight(session->course,&route,&simple,&work))
            goto invalid;
        PortableMeshCpuState candidate;
        const PortableMeshStatus status=portable_mesh_cpu_init(&candidate,session,index,error,capacity);
        if (status!=PORTABLE_MESH_OK)
            return status;
        if (!simple)
            unrankable=1;
    }
    if (unrankable)
        return portable_mesh_cpu_init(out,session,PORTABLE_MESH_ABSENT,error,capacity);
    for (uint32_t index=0;index<headers->count;++index)
    {
        PortableCourse3DRoute route;
        if (!portable_course_3d_route(session->course,index,&route))
            return planner_invalid(session,error,capacity);
        if (route.kind!=0u || route.flags!=1u)
            continue;
        PortableMeshCpuState candidate;
        const PortableMeshStatus status=portable_mesh_cpu_init(&candidate,session,index,error,capacity);
        if (status!=PORTABLE_MESH_OK)
            return status;
        uint64_t cost;
        if (!route_cost(session,&route,&candidate,&cost,&work))
            goto invalid;
        if (cost<best)
        {
            best=cost;
            selected=candidate;
        }
    }
    if (best==UINT64_MAX)
        goto invalid;
    *out=selected;
    return PORTABLE_MESH_OK;
invalid:
    if (error && capacity)
        snprintf(error,capacity,"3D route pilot course=%u: invalid route or nominal cost work budget exhausted (work=%u)",
                 session->course->id,work);
    return PORTABLE_MESH_UNSUPPORTED;
}
PortableMeshStatus portable_mesh_time_trial_pilot_step_route(PortableMeshTimeTrialPilotState *next,
    const PortableMeshTimeTrialPilotState *previous, const PortableMeshTimeTrialSession *session,
    uint32_t route_hint, PortableMeshKartInput *input, char *error, size_t capacity)
{
    if (!next || !previous || !session || !input || !session->course || !session->rules ||
        session->course->table_count > 32u)
        return planner_invalid(session,error,capacity);
    const PortableCourse3DTable *headers=portable_course_3d_table(session->course,
        PORTABLE_COURSE_3D_TAG('R','T','H','D'));
    int continuous=0;
    if (headers)
    {
        if (!headers->bytes || headers->stride!=32u || headers->count>16384u)
            return planner_invalid(session,error,capacity);
        for (uint32_t i=0;i<headers->count;++i)
        {
            PortableCourse3DRoute route;
            if (!portable_course_3d_route(session->course,i,&route))
                return planner_invalid(session,error,capacity);
            if (route.kind)
                continue;
            if (route.property_count>16384u || route.property_first>UINT32_MAX-route.property_count)
                return planner_invalid(session,error,capacity);
            for (uint32_t p=0;p<route.property_count;++p)
            {
                PortableCourse3DProperty property;
                if (!portable_course_3d_property_at(session->course,route.property_first+p,&property))
                    return planner_invalid(session,error,capacity);
                if (!strcmp(property.key,"mksc:route-pilot-continuous"))
                {
                    if (property.type!=PORTABLE_COURSE_3D_PROPERTY_BOOLEAN)
                        return planner_invalid(session,error,capacity);
                    continuous=(int)property.value.boolean;
                }
            }
            break; /* Matches production CPU and legacy pilot route selection. */
        }
    }
    if (route_hint!=PORTABLE_MESH_ABSENT)
    {
        PortableCourse3DRoute hinted;
        if (!continuous || (session->mode!=1u && session->mode!=4u && session->mode!=8u) ||
            !portable_course_3d_route(session->course,route_hint,&hinted) ||
            hinted.kind!=0u || hinted.flags!=1u)
        {
            if (error && capacity)
                snprintf(error,capacity,"3D route pilot course=%u mode=%u hint=%u: "
                    "explicit hint requires a closed continuous racing CPU route",
                    session->course->id,session->mode,route_hint);
            return PORTABLE_MESH_UNSUPPORTED;
        }
    }
    PortableMeshTimeTrialPilotState staged;
    memset(&staged,0,sizeof staged);
    PortableMeshKartInput control={0};
    PortableMeshStatus status;
    if (!continuous)
    {
        status=portable_mesh_time_trial_pilot(session,&control,error,capacity);
        if (status!=PORTABLE_MESH_OK)
            return status;
    }
    else
    {
        if (previous->active>1u)
            return planner_invalid(session,error,capacity);
        const int reset=!previous->active || previous->mode!=session->mode ||
            previous->cpu.course_id!=session->course->id ||
            previous->cpu.profile_id!=session->rules->profile_id ||
            previous->cpu.profile_revision!=session->rules->revision ||
            previous->requested_hint!=route_hint ||
            session->state.tick<previous->last_tick;
        if (reset)
        {
            status=route_hint!=PORTABLE_MESH_ABSENT ?
                portable_mesh_cpu_init(&staged.cpu,session,route_hint,error,capacity) :
                (session->mode==1u || session->mode==4u || session->mode==8u) ?
                nominal_route(&staged.cpu,session,error,capacity) :
                portable_mesh_cpu_init(&staged.cpu,session,PORTABLE_MESH_ABSENT,error,capacity);
            if (status!=PORTABLE_MESH_OK)
                return status;
        }
        else
        {
            staged=*previous;
            if (session->state.tick==previous->last_tick)
            {
                if ((previous->input.buttons & ~15u) || !portable_mesh_kart_rules_valid(session->rules) ||
                    !portable_mesh_kart_state_valid(&session->state.kart))
                    return planner_invalid(session,error,capacity);
                *next=staged;
                *input=staged.input;
                if (error && capacity) error[0]=0;
                return PORTABLE_MESH_OK;
            }
        }
        status=portable_mesh_cpu_input(&staged.cpu,&staged.cpu,session,&control,error,capacity);
        if (status!=PORTABLE_MESH_OK)
            return status;
        staged.active=1;
        staged.mode=session->mode;
        staged.requested_hint=route_hint;
        staged.last_tick=session->state.tick;
        staged.input=control;
    }
    *next=staged;
    *input=control;
    return PORTABLE_MESH_OK;
}
PortableMeshStatus portable_mesh_time_trial_pilot_step(PortableMeshTimeTrialPilotState *next,
    const PortableMeshTimeTrialPilotState *previous, const PortableMeshTimeTrialSession *session,
    PortableMeshKartInput *input, char *error, size_t capacity)
{
    return portable_mesh_time_trial_pilot_step_route(next,previous,session,
        PORTABLE_MESH_ABSENT,input,error,capacity);
}
/* Aid estimates only: the unchanged production controller still owns steering.
 * Limit the inspected route to a forward, nearly collinear ordinary-floor run;
 * never turn a geometric admission into a guarantee about traffic or tracking. */
static int boost_route_span(const PortableMeshTimeTrialSession *r,
    const PortableMeshTimeTrialPilotState *pilot, int32_t floor,
    int32_t *available, int32_t *target_speed)
{
    PortableCourse3DRoute route;
    if (!portable_course_3d_route(r->course,pilot->cpu.route,&route) || route.kind || route.flags!=1u ||
        !route.node_count || route.node_count>FINISH_GATE_LIMIT ||
        route.node_first>UINT32_MAX-route.node_count ||
        pilot->cpu.from_node<route.node_first || pilot->cpu.from_node-route.node_first>=route.node_count)
        return 0;
    uint32_t from=pilot->cpu.from_node;
    int32_t previous[3];memcpy(previous,r->state.kart.position_q16,sizeof previous);
    previous[2]=floor;
    int64_t distance=0;
    int32_t lowest=r->rules->forward_speed_q16;
    for(uint32_t walked=0;walked<route.node_count;++walked)
    {
        PortableCourse3DRouteNode a,b;uint32_t to;
        if(!portable_course_3d_route_node(r->course,from,&a) || a.edge_count!=1u ||
            !portable_course_3d_route_edge(r->course,a.edge_first,&to) ||
            to<route.node_first || to-route.node_first>=route.node_count ||
            !portable_course_3d_route_node(r->course,to,&b) ||
            (walked==0 && (to!=pilot->cpu.to_node || a.edge_first!=pilot->cpu.selected_edge))) return 0;
        int64_t raw[3];
        for(unsigned axis=0;axis<3;++axis)
        {
            if(b.position_q16[axis] < -PORTABLE_FIXED_3D_POSITION_LIMIT ||
                b.position_q16[axis]>PORTABLE_FIXED_3D_POSITION_LIMIT) return 0;
            raw[axis]=(int64_t)b.position_q16[axis]-previous[axis];
        }
        const int32_t cap=b.speed_q16>0 && b.speed_q16<r->rules->forward_speed_q16 ?
            b.speed_q16:r->rules->forward_speed_q16;
        if(cap<lowest) lowest=cap;
        int32_t direction[3];
        if(b.position_q16[2]!=floor || !portable_fixed_3d_normalize(raw,direction) ||
            portable_fixed_3d_dot(direction,r->state.kart.forward_q30)/U<FINISH_DIRECTION_COS)
        {
            if(pilot->cpu.corner_speed_q16<=0) return 0;
            if(pilot->cpu.corner_speed_q16<lowest) lowest=pilot->cpu.corner_speed_q16;
            break;
        }
        distance+=(raw[0]*r->state.kart.forward_q30[0]+raw[1]*r->state.kart.forward_q30[1])/U;
        if(distance>=FINISH_RANGE) {distance=FINISH_RANGE;break;}
        memcpy(previous,b.position_q16,sizeof previous);from=to;
    }
    if(distance<=0 || lowest<=0) return 0;
    *available=(int32_t)distance;*target_speed=lowest;return 1;
}
static int midrace_boost_window(const PortableMeshTimeTrialSession *r,
    const PortableMeshTimeTrialPilotState *pilot,const PortableMeshItemRules *items,
    uint16_t item,PortableMeshKartInput driving)
{
    const PortableMeshKartState *kart=&r->state.kart;
    if(!r->course || r->course->table_count>32u || !r->geometry || !pilot->active ||
        pilot->active>1u || !portable_mesh_kart_rules_valid(r->rules) ||
        !portable_mesh_kart_state_valid(kart) || !portable_mesh_item_rules_fit_profile(items,r->rules) ||
        (r->mode!=1u && r->mode!=4u && r->mode!=8u) || pilot->mode!=r->mode ||
        pilot->cpu.course_id!=r->course->id || pilot->cpu.profile_id!=r->rules->profile_id ||
        pilot->cpu.profile_revision!=r->rules->revision || r->state.phase!=PORTABLE_MESH_RACER_RACING ||
        r->state.respawn_grace || kart->respawn_ticks || !kart->grounded ||
        !level_kart(kart) ||
        kart->drift_direction || kart->drift_charge || kart->drift_tier || kart->outside_drift_q16 ||
        driving.steering < -2048 || driving.steering>2048 ||
        kart->steer_filtered_q16 < -4096 || kart->steer_filtered_q16>4096 ||
        pilot->cpu.lookahead_q16<0 || pilot->cpu.lookahead_q16>FINISH_RANGE) return 0;
    PortableMeshSurface surface;
    if(!r->geometry->surface ||
        !r->geometry->surface(r->geometry->context,kart->contact_surface,&surface) ||
        surface.type>2u || surface.speed_ratio_q16!=Q || surface.gravity_ratio_q16!=Q) return 0;
    const int64_t along=portable_fixed_3d_dot(kart->velocity_q16,kart->forward_q30)/U;
    if(along<=0) return 0;
    int64_t lateral[2];uint64_t lateral_squared=0,horizontal_squared=0;
    for(unsigned axis=0;axis<2;++axis)
    {
        lateral[axis]=(int64_t)kart->velocity_q16[axis]-along*kart->forward_q30[axis]/U;
        lateral_squared+=(uint64_t)(lateral[axis]*lateral[axis]);
        horizontal_squared+=(uint64_t)((int64_t)kart->velocity_q16[axis]*kart->velocity_q16[axis]);
    }
    const uint64_t lateral_speed=portable_fixed_3d_sqrt(lateral_squared);
    if(lateral_speed>Q/4u) return 0;
    const uint32_t duration=item==0x100au ? items->star_player_ticks:items->mushroom_boost_ticks;
    const int32_t ratio=item==0x100au ? items->star_cap_ratio_q16:items->mushroom_cap_ratio_q16;
    /* MK8: Star scales top speed; the mushroom scales at least 7.6 units/frame. */
    const int32_t top=r->rules->forward_speed_q16;
    const int64_t cap=(int64_t)(item==0x100au || top>MESH_KART_MK8_FLOOR_CAP_Q16 ? top:MESH_KART_MK8_FLOOR_CAP_Q16)*ratio/Q;
    const int32_t acceleration=item==0x100au ? items->star_acceleration_q16:
        items->mushroom_acceleration_q16;
    const int64_t step_acceleration=portable_fixed_3d_dt(acceleration);
    int64_t speed=(int64_t)portable_fixed_3d_sqrt(horizontal_squared);
    if(kart->speed_q16>speed) speed=kart->speed_q16;
    /* Lateral/rounding allowance is an estimate, not a norm invariant. The
     * effect REPLACES normal acceleration; estimate each clamped scalar turn,
     * rather than charging the full top speed for the entire active duration. */
    const int64_t allowance=(int64_t)lateral_speed+Q/2;
    int32_t available,target;
    const int64_t floor=(int64_t)kart->position_q16[2]-r->rules->kart_radius_q16-r->rules->ride_height_q16;
    if(floor < -PORTABLE_FIXED_3D_POSITION_LIMIT || floor>PORTABLE_FIXED_3D_POSITION_LIMIT ||
        !boost_route_span(r,pilot,(int32_t)floor,&available,&target)) return 0;
    const int64_t deceleration=r->rules->brake_deceleration_q16>r->rules->coast_deceleration_q16 ?
        r->rules->brake_deceleration_q16:r->rules->coast_deceleration_q16;
    if(deceleration<=0) return 0;
    int64_t distance=0;
    for(uint32_t tick=0;tick<duration;++tick)
    {
        speed+=step_acceleration;
        if(speed>cap) speed=cap;
        distance+=((speed+allowance)*4389+262143)/262144;
        if(distance>FINISH_RANGE) return 0;
    }
    speed+=allowance;
    if(speed>target) distance+=(speed*speed-(int64_t)target*target+2*deceleration-1)/(2*deceleration);
    distance+=pilot->cpu.lookahead_q16+(speed*2*4389+262143)/262144+r->rules->kart_radius_q16;
    if(distance<=0 || distance>FINISH_RANGE || distance>available) return 0;
    for(int side=-1;side<=1;++side)
    {
        PortableMeshTimeTrialSession probe=*r;
        int32_t end[3];
        for(unsigned axis=0;axis<3;++axis)
        {
            const int32_t right=axis==0 ? -kart->forward_q30[1] : axis==1 ? kart->forward_q30[0] : 0;
            const int64_t start=(int64_t)kart->position_q16[axis]+
                (int64_t)side*r->rules->kart_radius_q16*right/U;
            const int64_t finish=start+(int64_t)kart->forward_q30[axis]*distance/U;
            if(start < -PORTABLE_FIXED_3D_POSITION_LIMIT || start>PORTABLE_FIXED_3D_POSITION_LIMIT ||
                finish < -PORTABLE_FIXED_3D_POSITION_LIMIT || finish>PORTABLE_FIXED_3D_POSITION_LIMIT) return 0;
            probe.state.kart.position_q16[axis]=(int32_t)start;end[axis]=(int32_t)finish;
        }
        if(!finish_clear_line(&probe,end,(int32_t)floor)) return 0;
    }
    return 1;
}
uint8_t portable_mesh_time_trial_pilot_item_press_rules(const PortableMeshTimeTrialSession *r,
    const PortableMeshTimeTrialPilotState *pilot,const PortableMeshItemRacer *inventory,
    const PortableMeshItemRacerStatus *status,const PortableMeshItemRules *items,
    PortableMeshKartInput driving,uint8_t periodic_pressed)
{
    if(!r || !pilot || !inventory || !status || !portable_mesh_item_rules_valid(items)) return 0;
    if(!pilot->active) return periodic_pressed;
    const uint16_t item=inventory->held_item;
    if(item!=0x1007u && item!=0x1008u && item!=0x1009u && item!=0x100fu &&
        item!=0x100au && item!=0x100eu) return periodic_pressed;
    if(inventory->active || inventory->previous_item_pressed ||
        status->pending_mushroom || status->mushroom_ticks || status->star_ticks || status->spin_ticks ||
        (status->golden_ticks && item!=0x100eu) || (item==0x100eu && !status->golden_ticks) ||
        !(driving.buttons&PORTABLE_MESH_ACCELERATE) ||
        (driving.buttons&(PORTABLE_MESH_BRAKE|PORTABLE_MESH_HOP|PORTABLE_MESH_RESCUE)) ||
        r->state.kart.panel_ticks || r->state.kart.boost_ticks[0] ||
        r->state.kart.boost_ticks[1] || r->state.kart.boost_ticks[2]) return 0;
    PortableMeshFinishWindow finish;
    if(portable_mesh_time_trial_pilot_finish_window(r,pilot,&finish)) return 1;
    return (uint8_t)midrace_boost_window(r,pilot,items,item,driving);
}

PortableMeshStatus portable_mesh_time_trial_pilot(const PortableMeshTimeTrialSession *session,
                                                  PortableMeshKartInput *input, char *error,
                                                  size_t capacity)
{
    if (!session || !input || !session->course || !session->rules)
        return PORTABLE_MESH_INVALID;
    *input = (PortableMeshKartInput){0, 0};
    if (session->state.phase != PORTABLE_MESH_TT_RACING || session->state.kart.respawn_ticks)
        return PORTABLE_MESH_OK;
    const PortableCourse3DTable *headers =
        portable_course_3d_table(session->course, PORTABLE_COURSE_3D_TAG('R', 'T', 'H', 'D'));
    const PortableCourse3DTable *nodes =
        portable_course_3d_table(session->course, PORTABLE_COURSE_3D_TAG('R', 'T', 'N', 'D'));
    const PortableCourse3DTable *edges =
        portable_course_3d_table(session->course, PORTABLE_COURSE_3D_TAG('R', 'T', 'E', 'D'));
    uint32_t first = 0, count = 0;
    if (headers)
        for (uint32_t i = 0; i < headers->count; ++i)
        {
            const uint8_t *route = row(headers, i);
            if (word(route) == 0u)
            {
                first = word(route + 4u);
                count = word(route + 8u);
                break;
            }
        }
    if (!nodes || !edges || first > nodes->count || count > nodes->count - first || !count)
        return unavailable(session, error, capacity);
    const PortableMeshKartState *kart = &session->state.kart;
    uint64_t nearest = UINT64_MAX;
    uint32_t selected = PORTABLE_MESH_ABSENT, next_index = 0;
    int32_t selected_fraction = 0;
    for (uint32_t index = first; index < first + count; ++index)
    {
        const uint8_t *a = row(nodes, index);
        if (!word(a + 24u))
            continue;
        const uint8_t *edge = row(edges, word(a + 20u));
        if (!edge)
            return unavailable(session, error, capacity);
        const uint32_t next = word(edge);
        if (next < first || next >= first + count)
            return unavailable(session, error, capacity);
        const uint8_t *b = row(nodes, next);
        int32_t delta[3], relative[3];
        for (unsigned axis = 0; axis < 3u; ++axis)
        {
            delta[axis] = signed_word(b + axis * 4u) - signed_word(a + axis * 4u);
            relative[axis] = kart->position_q16[axis] - signed_word(a + axis * 4u);
        }
        const int64_t length_squared = portable_fixed_3d_dot(delta, delta);
        if (!length_squared)
            continue;
        const int32_t along = fraction(portable_fixed_3d_dot(relative, delta), length_squared);
        uint64_t square = 0;
        for (unsigned axis = 0; axis < 3u; ++axis)
        {
            const int64_t distance =
                (int64_t)relative[axis] - portable_fixed_3d_mul(delta[axis], along);
            square += (uint64_t)(distance * distance);
        }
        if (square < nearest)
        {
            nearest = square;
            selected = index;
            next_index = next;
            selected_fraction = along;
        }
    }
    if (selected == PORTABLE_MESH_ABSENT)
        return unavailable(session, error, capacity);
    const uint8_t *a = row(nodes, selected), *b = row(nodes, next_index);
    int32_t delta[3], target[3];
    for (unsigned axis = 0; axis < 3u; ++axis)
        delta[axis] = signed_word(b + axis * 4u) - signed_word(a + axis * 4u);
    const int64_t length =
        (int64_t)portable_fixed_3d_sqrt((uint64_t)portable_fixed_3d_dot(delta, delta));
    const int64_t remaining = length * (Q - selected_fraction) / Q;
    const int32_t lookahead = 2 * Q;
    if (remaining >= lookahead)
    {
        const int32_t ahead = selected_fraction + (int32_t)((int64_t)lookahead * Q / length);
        for (unsigned axis = 0; axis < 3u; ++axis)
            target[axis] = signed_word(a + axis * 4u) + portable_fixed_3d_mul(delta[axis], ahead);
    }
    else
    {
        memcpy(target, (int32_t[3]){signed_word(b), signed_word(b + 4u), signed_word(b + 8u)},
               sizeof(target));
        if (word(b + 24u))
        {
            const uint8_t *edge = row(edges, word(b + 20u));
            const uint8_t *following = edge ? row(nodes, word(edge)) : NULL;
            if (!following)
                return unavailable(session, error, capacity);
            int32_t direction[3];
            int64_t raw[3];
            for (unsigned axis = 0; axis < 3u; ++axis)
                raw[axis] = (int64_t)signed_word(following + axis * 4u) - target[axis];
            if (portable_fixed_3d_normalize(raw, direction))
                for (unsigned axis = 0; axis < 3u; ++axis)
                    target[axis] += (int32_t)((lookahead - remaining) * direction[axis] / U);
        }
    }
    /* Steering and speed use the ground tangent; height still disambiguates stacked routes above. */
    int64_t desired_raw[3];
    for (unsigned axis = 0; axis < 3u; ++axis)
        desired_raw[axis] = (int64_t)target[axis] - kart->position_q16[axis];
    const int64_t normal_distance =
        (desired_raw[0] * kart->up_q30[0] + desired_raw[1] * kart->up_q30[1] +
         desired_raw[2] * kart->up_q30[2]) /
        U;
    for (unsigned axis = 0; axis < 3u; ++axis)
        desired_raw[axis] -= normal_distance * kart->up_q30[axis] / U;
    int32_t desired[3];
    if (!portable_fixed_3d_normalize(desired_raw, desired))
    {
        input->buttons = PORTABLE_MESH_BRAKE;
        return PORTABLE_MESH_OK;
    }
    int32_t right[3];
    right[0] = (int32_t)(((int64_t)kart->up_q30[1] * kart->forward_q30[2] -
                          (int64_t)kart->up_q30[2] * kart->forward_q30[1]) /
                         U);
    right[1] = (int32_t)(((int64_t)kart->up_q30[2] * kart->forward_q30[0] -
                          (int64_t)kart->up_q30[0] * kart->forward_q30[2]) /
                         U);
    right[2] = (int32_t)(((int64_t)kart->up_q30[0] * kart->forward_q30[1] -
                          (int64_t)kart->up_q30[1] * kart->forward_q30[0]) /
                         U);
    const int64_t sine = portable_fixed_3d_dot(right, desired) / U,
                  cosine = portable_fixed_3d_dot(kart->forward_q30, desired) / U;
    int64_t steering = sine * 65534 / U;
    /* Sine alone vanishes near a target directly behind. Keep turning with
     * a stable sign instead of stalling on a nearly 180-degree heading error. */
    if (cosine < 0)
        steering = sine < 0 ? -32767 : 32767;
    input->steering = (int16_t)(steering > 32767 ? 32767 : steering < -32767 ? -32767 : steering);
    int32_t speed = signed_word(b + 16u);
    if (speed <= 0 || speed > session->rules->forward_speed_q16)
        speed = session->rules->forward_speed_q16;
    if (cosine < 966367642 && speed > 3 * Q)
        speed = 3 * Q;
    if (remaining < 6 * Q && word(b + 24u))
    {
        const uint8_t *edge = row(edges, word(b + 20u)),
                      *following = edge ? row(nodes, word(edge)) : NULL;
        if (!following)
            return unavailable(session, error, capacity);
        int64_t next_raw[3], current_raw[3];
        int32_t next_direction[3], current_direction[3];
        for (unsigned axis = 0; axis < 3u; ++axis)
        {
            next_raw[axis] =
                (int64_t)signed_word(following + axis * 4u) - signed_word(b + axis * 4u);
            current_raw[axis] = delta[axis];
        }
        if (portable_fixed_3d_normalize(next_raw, next_direction) &&
            portable_fixed_3d_normalize(current_raw, current_direction) &&
            portable_fixed_3d_dot(next_direction, current_direction) / U < 966367642 &&
            speed > 3 * Q)
            speed = 3 * Q;
    }
    /* Past half stick a brake starts MK8's brake slide, so the pilot coasts. */
    input->buttons = kart->speed_q16 <= speed + Q / 2                         ? PORTABLE_MESH_ACCELERATE
                     : input->steering < 16384 && input->steering > -16384 &&
                             kart->steer_filtered_q16 < Q / 2 && kart->steer_filtered_q16 > -Q / 2
                         ? PORTABLE_MESH_BRAKE
                                                                           : 0;
    if (error && capacity)
        error[0] = 0;
    return PORTABLE_MESH_OK;
}

/* Hazard seek bounds are aid admission choices, not physics or actor rules. */
#define SEEK_RANGE (15 * Q)
#define SEEK_BELOW (5 * Q / 2)
#define SEEK_ABOVE (6 * Q)
#define SEEK_PARK (Q / 2)      /* Kart centre this near the actor stops. */
#define SEEK_REACH (3 * Q / 2) /* An overshot actor this near also stops. */
#define SEEK_SHARE (8 * Q)     /* Actors this near an engaged one share its visit. */
#define SEEK_HOLD_LIMIT 900u   /* Fifteen seconds parked without a contact. */
#define SEEK_TURN (4 * Q)      /* Fastest overshoot the seek turns back for. */
#define SEEK_CONTACT (5 * Q / 2)      /* A visible actor this near is about to touch... */
#define SEEK_CONTACT_RISE (3 * Q / 2) /* ...and an actor this near kart height can touch. */
static uint8_t seek_engaged(const PortableMeshCourseActorEvents *e)
{
    /* Every racer effect except harmless bumps, protected contacts included. */
    return (uint8_t)(e->hit_mask | e->protected_mask | e->piranha_bite_mask |
        e->piranha_touch_mask | e->capture_mask | e->launch_mask | e->seagull_touch_mask |
        e->lava_burn_mask | e->thwomp_crush_mask | e->cannonball_hit_mask |
        e->cannonball_protected_mask | e->boo_touch_mask | e->rock_hit_mask |
        e->rock_protected_mask | e->pier_boo_touch_mask | e->pier_boo_protected_mask |
        e->kamek_touch_mask | e->kamek_protected_mask | e->mechakoopa_hit_mask |
        e->mechakoopa_protected_mask | e->mechakoopa_strong_mask | e->common_prop_hit_mask |
        e->common_prop_protected_mask | e->common_prop_umbrella_open_mask |
        e->common_prop_umbrella_close_mask | e->thundercloud_hit_mask |
        e->thundercloud_protected_mask | e->falling_star_hit_mask |
        e->falling_star_protected_mask | e->teepee_touch_mask | e->teepee_protected_mask);
}
/* Flat actor order follows portable_mesh_course_actors_config_count. */
static unsigned seek_family(const PortableMeshCourseActors *w, unsigned i)
{
    const uint32_t counts[] = {w->count, w->snowman_count, w->penguin_count, w->mouse_count,
        w->piranha_count, w->seagull_count, w->lava_count, w->thwomp_count, w->cannonball_count,
        w->boo_count, w->rock_count, w->teepee_count, w->falling_star_count, w->thundercloud_count,
        w->common_prop_count, w->mechakoopa_count, w->kamek_count, w->pier_boo_count};
    for (unsigned f = 0; f < sizeof counts / sizeof counts[0]; ++f)
    {
        if (i < counts[f])
            return f;
        i -= counts[f];
    }
    return 32u;
}
static int seek_contacted(const PortableMeshCourseActors *w, const PortableMeshCourseActorEvents *e,
                          unsigned i)
{
    const struct
    {
        uint32_t count;
        uint64_t contacts;
    } families[] = {{w->count, e->hit_actors | e->protected_actors},
                    {w->snowman_count, e->snowman_contacts},
                    {w->penguin_count, e->penguin_contacts},
                    {w->mouse_count, e->mouse_contacts},
                    {w->piranha_count, e->piranha_contacts},
                    {w->seagull_count, e->seagull_contacts},
                    {w->lava_count, e->lava_contacts},
                    {w->thwomp_count, e->thwomp_contacts},
                    {w->cannonball_count, e->cannonball_contacts},
                    {w->boo_count, e->boo_contacts},
                    {w->rock_count, e->rock_contacts},
                    {w->teepee_count, e->teepee_contacts},
                    {w->falling_star_count, e->falling_star_contacts},
                    {w->thundercloud_count, e->thundercloud_contacts},
                    {w->common_prop_count, e->common_prop_contacts},
                    {w->mechakoopa_count, e->mechakoopa_contacts},
                    {w->kamek_count, e->kamek_contacts},
                    {w->pier_boo_count, e->pier_boo_contacts}};
    for (size_t f = 0; f < sizeof families / sizeof families[0]; ++f)
    {
        if (i < families[f].count)
            return (int)((families[f].contacts >> i) & 1u);
        i -= families[f].count;
    }
    return 0;
}
static int64_t seek_square(const int32_t a[3], const int32_t b[3])
{
    const int64_t x = (int64_t)a[0] - b[0], y = (int64_t)a[1] - b[1];
    return x * x + y * y;
}
static void seek_visit(PortableMeshHazardSeekState *seek, const PortableMeshCourseActorsConfig *config,
                       const PortableMeshCourseActors *actors, unsigned count, unsigned target)
{
    PortableMeshCourseActorView anchor, view;
    seek->visited[target / 64u] |= UINT64_C(1) << (target % 64u);
    if (!portable_mesh_course_actors_view(config, actors, target, &anchor))
        return;
    for (unsigned i = 0; i < count; ++i)
        if (portable_mesh_course_actors_view(config, actors, i, &view) &&
            seek_square(view.position_q16, anchor.position_q16) <= (int64_t)SEEK_SHARE * SEEK_SHARE)
            seek->visited[i / 64u] |= UINT64_C(1) << (i % 64u);
}
int portable_mesh_time_trial_pilot_seek_hazard(PortableMeshHazardSeekState *seek,
    const PortableMeshRacer *racer, unsigned racer_index, const PortableMeshItems *items,
    const PortableMeshCourseActorsConfig *config, const PortableMeshCourseActors *actors,
    const PortableMeshCourseActorEvents *events, uint32_t family_mask, PortableMeshKartInput *input)
{
    if (!seek || !racer || !racer->course || !items || !config || !actors || !events || !input ||
        racer_index >= 8u || racer_index >= items->status.count)
        return 0;
    const unsigned count = portable_mesh_course_actors_count(actors);
    if (count > PORTABLE_MESH_HAZARD_SEEK_WORDS * 64u)
        return 0;
    const PortableMeshRacerState *state = &racer->state;
    if (seek->valid && seek->course_id == racer->course->id && seek->last_tick == state->tick)
    {
        if (seek->overriding)
            *input = seek->input;
        return 1;
    }
    PortableMeshHazardSeekState next = *seek;
    if (!next.valid || next.course_id != racer->course->id || state->tick < next.last_tick)
    {
        memset(&next, 0, sizeof next);
        next.valid = 1;
        next.course_id = racer->course->id;
        next.lap = state->lap;
    }
    if (next.lap != state->lap)
    {
        memset(next.visited, 0, sizeof next.visited);
        next.lap = state->lap;
    }
    next.last_tick = state->tick;
    const uint8_t bit = (uint8_t)(1u << racer_index);
    const PortableMeshKartState *kart = &state->kart;
    PortableMeshKartInput control = *input;
    int override = 0;
    const int contacted = next.target && next.target <= count && (seek_engaged(events) & bit) &&
                          seek_contacted(actors, events, next.target - 1u);
    if (contacted && next.contacts < UINT8_MAX)
        ++next.contacts;
    if (next.target && (next.target > count || ++next.hold_ticks > SEEK_HOLD_LIMIT || contacted))
    {
        if (next.target <= count)
            seek_visit(&next, config, actors, count, next.target - 1u);
        next.target = 0;
        next.hold_ticks = 0;
    }
    next.imminent = 0;
    if (!next.target)
    {
        next.visible = 0;
        next.goal_low = 0;
        next.fall_ticks = 0;
        next.tracked = 0;
        next.walked = 0;
    }
    if (items->status.racers[racer_index].boo_lock_ticks)
    {
        control = (PortableMeshKartInput){0, 0};
        override = 1;
    }
    else if (state->phase == PORTABLE_MESH_RACER_RACING && !kart->respawn_ticks &&
             !state->respawn_grace)
    {
        PortableMeshCourseActorView view;
        if (!next.target && kart->grounded)
        {
            int64_t best = (int64_t)SEEK_RANGE * SEEK_RANGE + 1;
            for (unsigned i = 0; i < count; ++i)
            {
                /* Hidden actors count too: a resting rock or lava bubble
                 * waits where it will drop or pop. */
                const unsigned family = seek_family(actors, i);
                if ((next.visited[i / 64u] >> (i % 64u)) & 1u ||
                    (family_mask && (family >= 32u || !((family_mask >> family) & 1u))) ||
                    !portable_mesh_course_actors_view(config, actors, i, &view))
                    continue;
                const int64_t dz = (int64_t)view.position_q16[2] - kart->position_q16[2];
                const int32_t delta[3] = {view.position_q16[0] - kart->position_q16[0],
                                          view.position_q16[1] - kart->position_q16[1], 0};
                const int64_t square = seek_square(view.position_q16, kart->position_q16);
                if (dz < -SEEK_BELOW || dz > SEEK_ABOVE || square >= best ||
                    portable_fixed_3d_dot(delta, kart->forward_q30) <= 0)
                    continue;
                best = square;
                next.target = i + 1u;
                memcpy(next.goal_q16, view.position_q16, sizeof next.goal_q16);
            }
        }
        if (next.target && portable_mesh_course_actors_view(config, actors, next.target - 1u, &view))
        {
            /* Park where the actor first came down to kart height, or where a
             * hidden one rests: its next pop, pass, fall or patrol comes back
             * through the same place. Keep this meeting point even while a low
             * patrol moves away or pauses elsewhere, rather than chasing it. */
            const int64_t rise = (int64_t)view.position_q16[2] - kart->position_q16[2];
            const int low = rise <= SEEK_CONTACT_RISE && rise >= -SEEK_CONTACT_RISE;
            const uint8_t was_visible = next.visible;
            next.visible = view.visible;
            if (low && (view.visible || view.collidable) && !next.goal_low)
            {
                memcpy(next.goal_q16, view.position_q16, sizeof next.goal_q16);
                next.goal_low = 1;
            }
            /* The target has walked across the ground since it was chosen. */
            if (next.tracked && seek_square(view.position_q16, next.last_position_q16) != 0)
                next.walked = 1;
            memcpy(next.last_position_q16, view.position_q16, sizeof next.last_position_q16);
            next.tracked = 1;
            const int64_t raw[3] = {(int64_t)next.goal_q16[0] - kart->position_q16[0],
                                    (int64_t)next.goal_q16[1] - kart->position_q16[1], 0};
            const int64_t square = raw[0] * raw[0] + raw[1] * raw[1];
            int32_t desired[3];
            int32_t right[3];
            right[0] = (int32_t)(((int64_t)kart->up_q30[1] * kart->forward_q30[2] -
                                  (int64_t)kart->up_q30[2] * kart->forward_q30[1]) / U);
            right[1] = (int32_t)(((int64_t)kart->up_q30[2] * kart->forward_q30[0] -
                                  (int64_t)kart->up_q30[0] * kart->forward_q30[2]) / U);
            right[2] = (int32_t)(((int64_t)kart->up_q30[0] * kart->forward_q30[1] -
                                  (int64_t)kart->up_q30[1] * kart->forward_q30[0]) / U);
            const int ahead = portable_fixed_3d_normalize(raw, desired) &&
                              portable_fixed_3d_dot(kart->forward_q30, desired) > 0;
            int64_t along = 0, lateral = 0, actor_along = 0, actor_lateral = 0;
            for (unsigned axis = 0; axis < 2u; ++axis)
            {
                along += raw[axis] * kart->forward_q30[axis] / U;
                lateral += raw[axis] * right[axis] / U;
                const int64_t delta = (int64_t)view.position_q16[axis] - kart->position_q16[axis];
                actor_along += delta * kart->forward_q30[axis] / U;
                actor_lateral += delta * right[axis] / U;
            }
            const int64_t side = lateral < 0 ? -lateral : lateral;
            const int64_t actor_side = actor_lateral < 0 ? -actor_lateral : actor_lateral;
            const int64_t actor_square = seek_square(view.position_q16, kart->position_q16);
            /* A held item waits until the kart's straight line runs through the
             * actual actor, not its meeting point: a Star fired mid-turn carries
             * the kart past at forced speed. Hidden collidable actors (Piranhas)
             * also need protection before the contact reveals them. */
            /* One dropping onto that line (a Thwomp) counts while it falls;
             * one lifting off it is gone before the kart arrives. Compare
             * actor heights, not relative rise: a kart climbing a ramp must
             * not mistake a stationary high actor for a descending one. A
             * single downward step can be a high actor's landing rumble;
             * require sustained descent before anticipating contact below it. */
            const int descending = was_visible && view.visible &&
                                   view.position_q16[2] < next.target_height_q16;
            next.fall_ticks = descending ? (uint8_t)(next.fall_ticks < 2 ? next.fall_ticks + 1 : 2) : 0;
            const int falling = next.fall_ticks >= 2 && rise > SEEK_CONTACT_RISE && rise <= SEEK_ABOVE;
            const int rising = was_visible && view.position_q16[2] > next.target_height_q16;
            next.imminent = (uint8_t)(view.collidable && (low || falling) && !rising &&
                                      actor_square <= (int64_t)SEEK_CONTACT * SEEK_CONTACT &&
                                      (actor_square <= (int64_t)Q * Q ||
                                       (actor_along > 0 && actor_side <= racer->rules->kart_radius_q16)));
            next.target_height_q16 = view.position_q16[2];
            /* MK8 turns no tighter than its low-speed arc: an actor inside
             * that circle is orbited, never reached. */
            const int circled = along * along + lateral * lateral <
                                2 * (int64_t)portable_mesh_kart_min_turn_radius(racer->rules) * side;
            if (!ahead && square > (int64_t)SEEK_REACH * SEEK_REACH && kart->speed_q16 > SEEK_TURN)
            {
                /* Overshot faster than the seek can turn back (a Star or
                 * boost forces speed): leave it rather than drive off the road. */
                seek_visit(&next, config, actors, count, next.target - 1u);
                next.target = 0;
                next.hold_ticks = 0;
            }
            /* A held item for a walker waits short of the meeting point, facing
             * it: the walk crosses the kart's straight line there, where a Star
             * carries the kart into it. Parked on the path, the walker reaches
             * the kart's side and the Star carries the kart off its path. */
            else if (square <= (int64_t)SEEK_PARK * SEEK_PARK ||
                     ((!ahead || circled || (next.item_pending && next.walked)) &&
                      square <= (int64_t)SEEK_REACH * SEEK_REACH))
            {
                control = (PortableMeshKartInput){0, kart->speed_q16 > Q / 4 ? PORTABLE_MESH_BRAKE : 0};
                override = 1;
            }
            else
            {
                const int64_t sine = portable_fixed_3d_dot(right, desired) / U;
                int64_t steering = circled ? 0 : ahead ? sine * 2 * 32767 / U : sine < 0 ? -32767 : 32767;
                if (steering > 32767)
                    steering = 32767;
                if (steering < -32767)
                    steering = -32767;
                /* Arrive slowly enough to stop on the actor: 0.8/s of the
                 * remaining distance, between 1.5 and 8 m/s. */
                int64_t speed = (int64_t)portable_fixed_3d_sqrt((uint64_t)square) * 4 / 5;
                speed = speed < 3 * Q / 2 ? 3 * Q / 2 : speed > 8 * Q ? 8 * Q : speed;
                control.steering = (int16_t)steering;
                control.buttons = kart->speed_q16 > speed + Q ? PORTABLE_MESH_BRAKE
                                  : kart->speed_q16 > speed   ? 0
                                                              : PORTABLE_MESH_ACCELERATE;
                override = 1;
            }
        }
    }
    next.input = control;
    next.overriding = (uint8_t)override;
    *seek = next;
    if (override)
        *input = control;
    return 1;
}
uint8_t portable_mesh_time_trial_pilot_seek_item(PortableMeshHazardSeekState *seek, uint8_t pressed)
{
    if (!seek)
        return pressed;
    if (seek->overriding && seek->target && !seek->imminent)
    {
        seek->item_pending |= pressed;
        return 0;
    }
    if (!seek->item_pending)
        return pressed;
    seek->item_pending = 0;
    return 1;
}
