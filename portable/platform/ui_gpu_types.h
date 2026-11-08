#ifndef PORTABLE_UI_GPU_TYPES_H
#define PORTABLE_UI_GPU_TYPES_H

#include "ui_draw.h"
#include <stddef.h>

#define UI_GPU_MAX_EDGES 160u
#define UI_GPU_UNIFORM_BYTES 2752u
/* Straight RGBA bytes enter the paint; the fragment output is premultiplied.
 * Geometry supplies UV [0,1], independent of the sampled image subrectangle. */
typedef struct UiGpuUniforms {
    float quad[4];
    float rectangle[4];
    float shape[4];
    float effect[4];
    float paint_axis[4];
    float paint_positions[4];
    float paint_colors[4][4];
    float source[4];
    float auxiliary[4];
    float edges[UI_GPU_MAX_EDGES][4];
} UiGpuUniforms;

_Static_assert(sizeof(float) == 4, "UI uniforms require binary32 floats");
_Static_assert(sizeof(UiGpuUniforms) == UI_GPU_UNIFORM_BYTES, "UI uniform layout");
_Static_assert(offsetof(UiGpuUniforms, paint_colors) == 96, "UI paint offset");
_Static_assert(offsetof(UiGpuUniforms, source) == 160, "UI source offset");
_Static_assert(offsetof(UiGpuUniforms, edges) == 192, "UI edge offset");
_Static_assert(UI_DRAW_CLEAR == 0 && UI_DRAW_CONVEX == 1 && UI_DRAW_ROUND_RECT == 2 &&
    UI_DRAW_ELLIPSE == 3 && UI_DRAW_STROKE_CIRCLE == 4 && UI_DRAW_STROKE_ROUND_RECT == 5 &&
    UI_DRAW_RECT == 6 && UI_DRAW_IMAGE == 7 && UI_DRAW_TINT == 8 && UI_DRAW_GLYPH == 9 &&
    UI_DRAW_CHECKER == 10, "UI shader kind mapping");
#endif
