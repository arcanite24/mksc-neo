#include "course_editor.h"
#include <string.h>
#include <stdlib.h>
const char *course_editor_kind_name(unsigned kind)
{
    static const char *names[] = {"Road", "Wall", "Ramp", "Boost pad", "Item box",
        "Start grid", "Gate", "Respawn", "Decoration"};
    return kind < EDITOR_KINDS ? names[kind] : "Unknown";
}
int course_editor_valid(const CourseEditorDocument *d)
{
    if (!d || d->count > EDITOR_LIMIT) return 0;
    for (unsigned i=0; i<d->count; ++i) {
        const CourseEditorPiece *p=&d->pieces[i];
        if (p->kind>=EDITOR_KINDS || p->rotation>3 || p->x < -400 || p->x>400 ||
            p->y < -400 || p->y>400 || p->z < -32 || p->z>64 || p->x%4 || p->y%4) return 0;
    }
    return 1;
}
static void remember(CourseEditor *e)
{
    if (e->history_cursor+1 == EDITOR_HISTORY) {
        memmove(e->history,e->history+1,(EDITOR_HISTORY-1)*sizeof e->history[0]);
        --e->history_cursor;
    }
    e->history[++e->history_cursor]=e->document;
    e->history_count=e->history_cursor+1;
}
void course_editor_new(CourseEditor *e)
{
    memset(e,0,sizeof *e); e->selected=-1;
    for (int y=-16;y<=16;y+=8) for (int x=-16;x<=16;x+=8)
        e->document.pieces[e->document.count++]=(CourseEditorPiece){x,y,0,EDITOR_ROAD,0};
    const CourseEditorPiece markers[] = {{8,12,0,EDITOR_START,0},{8,8,0,EDITOR_GATE,0},
        {8,-8,0,EDITOR_GATE,0},{-8,-8,0,EDITOR_GATE,3},{-8,8,0,EDITOR_GATE,2},
        {8,12,0,EDITOR_RESPAWN,0}};
    for (unsigned i=0;i<sizeof markers/sizeof markers[0];++i)
        e->document.pieces[e->document.count++]=markers[i];
    e->history[0]=e->document;e->history_count=1;
}
int course_editor_put(CourseEditor *e, CourseEditorPiece p)
{
    CourseEditorDocument candidate=e->document;
    /* An identical piece at the same anchor adds nothing but a hidden duplicate. */
    for (unsigned i=0;i<candidate.count;++i)
        if ((int)i!=e->selected && !memcmp(&candidate.pieces[i],&p,sizeof p)) return 0;
    if (e->selected<0) {if (candidate.count==EDITOR_LIMIT) return 0;candidate.pieces[candidate.count++]=p;}
    else {if ((unsigned)e->selected>=candidate.count) return 0;candidate.pieces[e->selected]=p;}
    if (!course_editor_valid(&candidate) || !memcmp(&candidate,&e->document,sizeof candidate)) return 0;
    e->document=candidate;remember(e);return 1;
}
int course_editor_delete(CourseEditor *e)
{
    if(e->selected<0 || (unsigned)e->selected>=e->document.count)return 0;
    unsigned at=(unsigned)e->selected;
    memmove(e->document.pieces+at,e->document.pieces+at+1,
        (e->document.count-at-1)*sizeof e->document.pieces[0]);
    memset(&e->document.pieces[--e->document.count],0,sizeof e->document.pieces[0]);
    e->selected=-1;remember(e);return 1;
}
int course_editor_undo(CourseEditor *e,int redo)
{
    if (redo ? e->history_cursor+1>=e->history_count : e->history_cursor==0)return 0;
    if(redo)++e->history_cursor;else --e->history_cursor;
    e->document=e->history[e->history_cursor];e->selected=-1;return 1;
}
int course_editor_pick(const CourseEditor *e,int x,int y,int z)
{
    for(unsigned i=e->document.count;i>0;--i){const CourseEditorPiece *p=&e->document.pieces[i-1];
        if(abs(p->x-x)<=3 && abs(p->y-y)<=3 && p->z==z)return (int)i-1;}
    return -1;
}
