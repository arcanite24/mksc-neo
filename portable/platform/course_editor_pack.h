#ifndef PORTABLE_COURSE_EDITOR_PACK_H
#define PORTABLE_COURSE_EDITOR_PACK_H
#include "course_editor.h"
#include <stddef.h>
#include "asset_pack.h"
/* Native C3D1 + C3C1 + ordinary authored profiles, accepted by the mod loader.
 * Output is malloc-owned. Does not borrow the document or any game assets. */
int course_editor_pack(const CourseEditorDocument *, uint32_t id, const char *name, uint8_t **bytes,
    size_t *size, char *error, size_t capacity);
int course_editor_unpack(const PortableAssetPackView *, uint32_t id, CourseEditorDocument *,
    char *error, size_t capacity);
#endif
