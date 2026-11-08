#ifndef PORTABLE_MESH_CUP_MENU_H
#define PORTABLE_MESH_CUP_MENU_H

#include "frontend_flow.h"

/* One 3D catalog row with its group (providing mod) already resolved. */
typedef struct
{
    uint32_t course_id, cup_id, slot, modes, sort_key;
    const char *name, *cup_name;
    uint8_t group, raceable;
} PortableMeshCupMenuRow;

/* Folds catalog rows into menu cups: groups in ascending order, cups in the
 * order of their first course (sort key, then ID), rows by cup slot. A cup
 * with more than eight courses continues in another entry. gp marks exactly
 * four GP courses in slots 0..3. Returns the cup count, or 0 with *overflow
 * set when capacity is too small (nothing partial is published). */
size_t portable_mesh_cup_menu_build(const PortableMeshCupMenuRow *rows, size_t count,
                                    PortableFrontendMeshCup *cups, size_t capacity,
                                    int *overflow);

#endif
