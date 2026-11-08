#include "battle_navigation.h"

static int clear_point(const PortableTerrainView *terrain,
    const PortableTrackMaterialView *material,int32_t x,int32_t y,unsigned clearance)
{
    for (int dy=-(int)clearance;dy<=(int)clearance;dy+=(int)clearance) {
        for (int dx=-(int)clearance;dx<=(int)clearance;dx+=(int)clearance) {
            uint8_t tile,flags;
            if (portable_terrain_query8(terrain,x+dx,y+dy,&tile)!=0 ||
                portable_track_material_query(material,tile,&flags)!=0 ||
                !(flags<0x20u || (flags>=0x40u && flags<0x80u) || flags==0xc0u))
                return 0;
        }
    }
    return 1;
}

static int line_clear(const PortableTerrainView *terrain,
    const PortableTrackMaterialView *material,PortableBattleNavPoint a,
    PortableBattleNavPoint b,unsigned clearance)
{
    const int32_t dx=b.x-a.x,dy=b.y-a.y;
    const unsigned ax=(unsigned)(dx<0?-dx:dx),ay=(unsigned)(dy<0?-dy:dy);
    const unsigned steps=(ax>ay?ax:ay)/4u+1u;
    for (unsigned i=0u;i<=steps;++i)
        if (!clear_point(terrain,material,a.x+(int32_t)((int64_t)dx*i/steps),
                a.y+(int32_t)((int64_t)dy*i/steps),clearance)) return 0;
    return 1;
}

static PortableBattleNavPoint center(unsigned cell,unsigned width)
{
    return (PortableBattleNavPoint){(int32_t)(cell%width)*16+8,(int32_t)(cell/width)*16+8};
}

static uint16_t nearest_cell(const PortableBattleNavWorkspace *work,unsigned width,
    unsigned height,PortableBattleNavPoint point)
{
    const int cx=point.x/16,cy=point.y/16;
    uint32_t best=UINT32_MAX;
    uint16_t result=UINT16_MAX;
    for (int dy=-2;dy<=2;++dy) for (int dx=-2;dx<=2;++dx) {
        const int x=cx+dx,y=cy+dy;
        if (x<0 || y<0 || (unsigned)x>=width || (unsigned)y>=height) continue;
        const unsigned cell=(unsigned)y*width+(unsigned)x;
        if (!work->passable[cell]) continue;
        const PortableBattleNavPoint p=center(cell,width);
        const int32_t px=p.x-point.x,py=p.y-point.y;
        const uint32_t distance=(uint32_t)(px*px+py*py);
        if (distance<best) { best=distance;result=(uint16_t)cell; }
    }
    return result;
}

int portable_battle_navigation_route(PortableBattleNavWorkspace *work,
    const PortableTerrainView *terrain,const PortableTrackMaterialView *material,
    const PortableBattleAiRules *rules,PortableBattleNavPoint start,
    const PortableBattleNavPoint *goals,size_t goal_count,PortableBattleNavRoute *out)
{
    if (work==NULL || terrain==NULL || terrain->cells==NULL || material==NULL ||
        material->flags==NULL || material->count!=256u || out==NULL ||
        !portable_battle_ai_rules_valid(rules) || goals==NULL || goal_count==0u ||
        goal_count>PORTABLE_BATTLE_NAV_GOALS || terrain->width==0u || terrain->height==0u ||
        terrain->width>256u || terrain->height>256u || terrain->row_stride<terrain->width ||
        start.x<0 || start.y<0 || start.x>=(int32_t)terrain->width*8 ||
        start.y>=(int32_t)terrain->height*8) return 0;
    const unsigned width=(unsigned)(terrain->width+1u)/2u;
    const unsigned height=(unsigned)(terrain->height+1u)/2u;
    const unsigned cells=width*height;
    *out=(PortableBattleNavRoute){0};
    for (unsigned i=0u;i<cells;++i) {
        const PortableBattleNavPoint p=center(i,width);
        work->passable[i]=(uint8_t)clear_point(terrain,material,p.x,p.y,rules->clearance);
        work->parent[i]=UINT16_MAX;
    }
    uint16_t goal_cells[PORTABLE_BATTLE_NAV_GOALS];
    for (size_t i=0u;i<goal_count;++i) {
        if (goals[i].x<0 || goals[i].y<0 || goals[i].x>=(int32_t)terrain->width*8 ||
            goals[i].y>=(int32_t)terrain->height*8) goal_cells[i]=UINT16_MAX;
        else goal_cells[i]=nearest_cell(work,width,height,goals[i]);
    }
    const uint16_t origin=nearest_cell(work,width,height,start);
    if (origin==UINT16_MAX) return 1;
    unsigned head=0u,tail=1u;
    work->queue[0]=origin;work->parent[origin]=origin;
    uint16_t destination=UINT16_MAX;
    while (head<tail && destination==UINT16_MAX) {
        const uint16_t cell=work->queue[head++];
        for (size_t g=0u;g<goal_count;++g) if (goal_cells[g]==cell) {
            destination=cell;out->goal=(uint16_t)g;break;
        }
        if (destination!=UINT16_MAX) break;
        const int x=cell%width,y=cell/width;
        const int nx[4]={x,x+1,x,x-1},ny[4]={y-1,y,y+1,y};
        for (unsigned d=0u;d<4u;++d) {
            if (nx[d]<0 || ny[d]<0 || (unsigned)nx[d]>=width || (unsigned)ny[d]>=height) continue;
            const unsigned n=(unsigned)ny[d]*width+(unsigned)nx[d];
            if (!work->passable[n] || work->parent[n]!=UINT16_MAX) continue;
            work->parent[n]=cell;work->queue[tail++]=(uint16_t)n;
        }
    }
    if (destination==UINT16_MAX) return 1;
    out->reachable=1u;out->waypoint=center(origin,width);
    const int64_t limit=(int64_t)rules->lookahead*rules->lookahead;
    const PortableBattleNavPoint goal=goals[out->goal];
    const int64_t gx=(int64_t)goal.x-start.x,gy=(int64_t)goal.y-start.y;
    if (gx*gx+gy*gy<=limit && line_clear(terrain,material,start,goal,rules->clearance)) {
        out->waypoint=goal;return 1;
    }
    /* Walk backward: the first visible node within lookahead is the furthest
     * useful waypoint. Every segment is swept with the same kart clearance. */
    uint16_t cell=destination;
    while (cell!=origin) {
        const PortableBattleNavPoint p=center(cell,width);
        const int64_t dx=(int64_t)p.x-start.x,dy=(int64_t)p.y-start.y;
        if (dx*dx+dy*dy<=limit && line_clear(terrain,material,start,p,rules->clearance)) {
            out->waypoint=p;break;
        }
        cell=work->parent[cell];
    }
    return 1;
}
