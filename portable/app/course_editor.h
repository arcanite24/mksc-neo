#ifndef PORTABLE_COURSE_EDITOR_H
#define PORTABLE_COURSE_EDITOR_H
#include <stdint.h>
enum { EDITOR_LIMIT = 128, EDITOR_HISTORY = 32, EDITOR_KINDS = 9 };
typedef enum { EDITOR_ROAD, EDITOR_WALL, EDITOR_RAMP, EDITOR_BOOST, EDITOR_ITEM,
    EDITOR_START, EDITOR_GATE, EDITOR_RESPAWN, EDITOR_DECORATION } CourseEditorKind;
/* Integer metres, snapped to four metres horizontally and one vertically. */
typedef struct { int32_t x, y, z; uint32_t kind, rotation; } CourseEditorPiece;
typedef struct { uint32_t count; CourseEditorPiece pieces[EDITOR_LIMIT]; } CourseEditorDocument;
typedef struct {
    CourseEditorDocument document, history[EDITOR_HISTORY];
    unsigned history_count, history_cursor;
    int selected;
} CourseEditor;
void course_editor_new(CourseEditor *editor);
int course_editor_valid(const CourseEditorDocument *document);
int course_editor_put(CourseEditor *editor, CourseEditorPiece piece);
int course_editor_delete(CourseEditor *editor);
int course_editor_undo(CourseEditor *editor, int redo);
int course_editor_pick(const CourseEditor *editor, int x, int y, int z);
const char *course_editor_kind_name(unsigned kind);
#endif
