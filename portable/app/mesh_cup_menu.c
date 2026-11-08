#include "mesh_cup_menu.h"

#include <string.h>

static int row_before(const PortableMeshCupMenuRow *a, const PortableMeshCupMenuRow *b)
{
    if (a->group != b->group)
        return a->group < b->group;
    if (a->sort_key != b->sort_key)
        return a->sort_key < b->sort_key;
    return a->course_id < b->course_id;
}

static int slot_before(const PortableMeshCupMenuRow *a, const PortableMeshCupMenuRow *b)
{
    if (a->slot != b->slot)
        return a->slot < b->slot;
    return row_before(a, b);
}

size_t portable_mesh_cup_menu_build(const PortableMeshCupMenuRow *rows, size_t count,
                                    PortableFrontendMeshCup *cups, size_t capacity,
                                    int *overflow)
{
    if (overflow)
        *overflow = 0;
    if (!rows || !cups || count == 0u)
        return 0u;
    size_t made = 0u;
    /* Each pass takes the earliest unplaced row; its cup is every unplaced
     * row of that group and cup ID. Quadratic, but catalogs stay small. */
    uint8_t placed_storage[1024];
    if (count > sizeof placed_storage)
    {
        if (overflow)
            *overflow = 1;
        return 0u;
    }
    memset(placed_storage, 0, count);
    for (;;)
    {
        size_t first = count;
        for (size_t i = 0u; i < count; ++i)
            if (!placed_storage[i] && (first == count || row_before(&rows[i], &rows[first])))
                first = i;
        if (first == count)
            break;
        const uint8_t group = rows[first].group;
        const uint32_t cup_id = rows[first].cup_id;
        for (;;)
        {
            /* Rows of this cup by slot, at most eight per entry. */
            size_t members[PORTABLE_FRONTEND_MESH_CUP_COURSES];
            unsigned taken = 0u;
            while (taken < PORTABLE_FRONTEND_MESH_CUP_COURSES)
            {
                size_t next = count;
                for (size_t i = 0u; i < count; ++i)
                    if (!placed_storage[i] && rows[i].group == group && rows[i].cup_id == cup_id &&
                        (next == count || slot_before(&rows[i], &rows[next])))
                        next = i;
                if (next == count)
                    break;
                placed_storage[next] = 1u;
                members[taken++] = next;
            }
            if (taken == 0u)
                break;
            if (made == capacity)
            {
                if (overflow)
                    *overflow = 1;
                return 0u;
            }
            PortableFrontendMeshCup *cup = &cups[made++];
            memset(cup, 0, sizeof *cup);
            cup->cup_id = cup_id;
            cup->group = group;
            cup->course_count = (uint8_t)taken;
            cup->name = rows[members[0]].cup_name;
            unsigned gp_slots = 0u;
            for (unsigned row = 0u; row < taken; ++row)
            {
                const PortableMeshCupMenuRow *source = &rows[members[row]];
                cup->course_ids[row] = source->course_id;
                cup->course_modes[row] = source->modes;
                cup->course_names[row] = source->name;
                if (!source->raceable)
                    cup->explore_only = (uint8_t)(cup->explore_only | (1u << row));
                if ((source->modes & 1u) && source->slot < 4u)
                    gp_slots |= 1u << source->slot;
                if ((!cup->name || !cup->name[0]) && source->cup_name && source->cup_name[0])
                    cup->name = source->cup_name;
            }
            cup->gp = (uint8_t)(taken == 4u && gp_slots == 15u);
        }
    }
    return made;
}
