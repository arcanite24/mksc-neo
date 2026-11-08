#ifndef PORTABLE_COURSE_EDITOR_HOST_H
#define PORTABLE_COURSE_EDITOR_HOST_H
#include "course_3d_host.h"
typedef struct CourseEditorHost CourseEditorHost;
CourseEditorHost *course_editor_host_create(const char *mods_directory);
void course_editor_host_close(CourseEditorHost *);
int course_editor_host_active(const CourseEditorHost *);
/* Returns one when the editor consumed a desktop event. */
int course_editor_host_event(CourseEditorHost *,const SDL_Event *,void *window);
void course_editor_host_tick(CourseEditorHost *,uint16_t buttons);
int course_editor_host_render(ModernMenu *,UiCanvas *,void *);
/* A requested test uses the ordinary 3D host with a composed content view.
 * The shell owns driving input/audio; this owner retains content until return. */
PortableCourse3DHost *course_editor_host_test(CourseEditorHost *,const PortableAssetPackView *,const PortableSettings *);
void course_editor_host_test_return(CourseEditorHost *);
#endif
