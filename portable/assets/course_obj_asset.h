#ifndef PORTABLE_COURSE_OBJ_ASSET_H
#define PORTABLE_COURSE_OBJ_ASSET_H
#include "track_asset.h"
typedef struct {
    uint8_t blocks[2][4096], palette[96];
    size_t block_bytes[2], palette_bytes;
    const uint8_t *resource;
    size_t resource_bytes;
    uint32_t relative_refs[3];
} PortableCourseObjAsset;
enum { PORTABLE_COURSE_OBJ_OK=0, PORTABLE_COURSE_OBJ_INVALID=1,
    PORTABLE_COURSE_OBJ_UNSUPPORTED=2 };
/* Selected EC0 split OBJ branch; zero split offsets and absent palette retain
 * source semantics. Unsplit/alternate inputs explicitly unsupported. Decode
 * atomically into owned asset storage; resource bytes remain borrowed/immutable. */
int portable_course_obj_asset_build(const PortableTrackAsset *track,
    PortableCourseObjAsset *asset);
#endif
