#include "model_asset.h"
#include "../app/diagnostic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t read_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
}

static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int32_t read_i32(const uint8_t *p)
{
    uint32_t value = read_u32(p);
    return value <= INT32_MAX ? (int32_t)value : -1 - (int32_t)(UINT32_MAX - value);
}

/* Authored model sections carry no flags; imported ones are required and
 * deterministic like every other imported section. */
static int optional_flags(uint32_t flags)
{
    return flags == 0u || flags == PORTABLE_ASSET_SECTION_DETERMINISTIC ||
           flags == (PORTABLE_ASSET_SECTION_REQUIRED | PORTABLE_ASSET_SECTION_DETERMINISTIC);
}

static int find_optional(const PortableAssetPackView *pack, uint32_t kind, uint32_t id,
                         PortableAssetSectionView *section)
{
    return portable_asset_pack_find_section(pack, kind, id, section) == PORTABLE_ASSET_PACK_OK &&
           section->version == 1 && optional_flags(section->flags) && section->bytes != NULL;
}

static int open_material(const PortableAssetPackView *pack, uint32_t id, PortableModelMaterial *out)
{
    PortableAssetSectionView section;
    if (!id || !find_optional(pack, PORTABLE_ASSET_SECTION_MODEL_MATERIAL, id, &section) ||
        section.size < 16 || read_u32(section.bytes) != 0x314c544du)
        return 0;
    const uint8_t *p = section.bytes;
    uint32_t texture_size = read_u32(p + 12);
    if ((read_u32(p + 8) & ~(uint32_t)(15u | PORTABLE_MODEL_BLEND)) || (size_t)texture_size != section.size - 16)
        return 0;
    memset(out, 0, sizeof(*out));
    out->argb = read_u32(p + 4);
    out->flags = read_u32(p + 8);
    if (!texture_size)
        return 1;
    p += 16;
    /* IMG1 is direct ARGB: opening remains bounded even for shared materials. */
    return texture_size >= 32 && memcmp(p, "MKSCIMG1", 8) == 0 &&
           portable_native_image_open(p, texture_size, &out->texture) &&
           out->texture.width <= PORTABLE_MODEL_MAX_TEXTURE_EXTENT &&
           out->texture.height <= PORTABLE_MODEL_MAX_TEXTURE_EXTENT;
}

int portable_model_material_open(const PortableAssetPackView *pack, uint32_t id,
                                 PortableModelMaterial *out)
{
    PortableModelMaterial candidate;
    if (!pack || !out || !open_material(pack, id, &candidate))
        return 0;
    *out = candidate;
    return 1;
}

static int open_mesh(const PortableAssetPackView *pack, uint32_t id, PortableModelView *out)
{
    PortableAssetSectionView section;
    if (!id || !find_optional(pack, PORTABLE_ASSET_SECTION_MODEL_MESH, id, &section) ||
        section.size < 16 || read_u32(section.bytes) != 0x3148534du)
        return 0;
    const uint8_t *p = section.bytes;
    uint32_t vertices = read_u32(p + 4), triangles = read_u32(p + 8), materials = read_u32(p + 12);
    if (!vertices || vertices > PORTABLE_MODEL_MAX_VERTICES || !triangles ||
        triangles > PORTABLE_MODEL_MAX_TRIANGLES || !materials ||
        materials > PORTABLE_MODEL_MAX_MATERIALS ||
        section.size !=
            16u + (size_t)materials * 4u + (size_t)vertices * 20u + (size_t)triangles * 8u)
        return 0;
    memset(out, 0, sizeof(*out));
    out->vertex_count = vertices;
    out->triangle_count = triangles;
    out->material_count = materials;
    for (uint32_t i = 0; i < materials; ++i)
        if (!open_material(pack, read_u32(p + 16 + i * 4), &out->materials[i]))
            return 0;
    out->vertices = p + 16 + materials * 4;
    out->triangles = out->vertices + vertices * 20;
    for (uint32_t i = 0; i < triangles; ++i)
    {
        const uint8_t *row = out->triangles + i * 8;
        if (read_u16(row) >= vertices || read_u16(row + 2) >= vertices ||
            read_u16(row + 4) >= vertices || read_u16(row + 6) >= materials)
            return 0;
    }
    return 1;
}

int portable_model_mesh_open(const PortableAssetPackView *pack, uint32_t id, PortableModelView *out)
{
    PortableModelView candidate;
    if (!pack || !out || !open_mesh(pack, id, &candidate))
        return 0;
    *out = candidate;
    return 1;
}

static int same_key(const uint8_t *a, const uint8_t *b)
{
    return memcmp(a, b, 16) == 0;
}

static int binding_registry(const PortableAssetSectionView *section, uint32_t version,
                            uint32_t *count)
{
    if (!section || section->kind != PORTABLE_ASSET_SECTION_MODEL_BINDINGS ||
        section->content_id != 1 || section->version != version || !optional_flags(section->flags) ||
        !section->bytes || section->size < 8 || read_u32(section->bytes) != 0x31444e42u)
        return 0;
    const uint32_t rows = read_u32(section->bytes + 4);
    if (rows > PORTABLE_MODEL_MAX_BINDINGS || section->size != 8u + (size_t)rows * 44u)
        return 0;
    *count = rows;
    return 1;
}
static int overlay_error(char *error, size_t capacity, const char *reason)
{
    if (error && capacity)
        snprintf(error, capacity, "Models: BND1 overlay: %s", reason);
    return 0;
}
static int key_before(const uint8_t *a, const uint8_t *b)
{
    for (unsigned field = 0; field < 4; ++field)
    {
        const uint32_t left = read_u32(a + field * 4), right = read_u32(b + field * 4);
        if (left != right)
            return left < right;
    }
    return 0;
}
void portable_model_bindings_overlay_close(PortableModelBindingOverlay *owner)
{
    if (owner)
    {
        free(owner->bytes);
        memset(owner, 0, sizeof *owner);
    }
}
int portable_model_bindings_overlay_prepare(const PortableAssetSectionView *previous,
                                            const PortableAssetSectionView *overlay,
                                            PortableModelBindingOverlay *out, char *error,
                                            size_t capacity)
{
    uint32_t incoming = 0, inherited = 0;
    if (!out || !binding_registry(overlay, 2, &incoming))
        return overlay_error(error, capacity, "invalid version-2 registry");
    if (previous && previous->version != 1)
        return overlay_error(error, capacity, "unsupported predecessor version");
    if (previous)
        (void)binding_registry(previous, 1, &inherited);
    const uint8_t *rows[PORTABLE_MODEL_MAX_BINDINGS];
    uint32_t count = 0;
    for (uint32_t i = 0; i < inherited; ++i)
    {
        const uint8_t *row = previous->bytes + 8 + (size_t)i * 44;
        int replaced = 0;
        for (uint32_t j = 0; j < incoming; ++j)
            if (same_key(row, overlay->bytes + 8 + (size_t)j * 44))
            {
                replaced = 1;
                break;
            }
        if (!replaced)
            rows[count++] = row;
    }
    if (incoming > PORTABLE_MODEL_MAX_BINDINGS - count)
        return overlay_error(error, capacity, "merged registry exceeds 256 raw rows");
    for (uint32_t i = 0; i < incoming; ++i)
        rows[count++] = overlay->bytes + 8 + (size_t)i * 44;
    /* Insertion sort is bounded at 256 rows and stable for equal keys. */
    for (uint32_t i = 1; i < count; ++i)
    {
        const uint8_t *row = rows[i];
        uint32_t at = i;
        while (at && key_before(row, rows[at - 1]))
        {
            rows[at] = rows[at - 1];
            --at;
        }
        rows[at] = row;
    }
    PortableModelBindingOverlay next = {0};
    next.size = 8u + (size_t)count * 44u;
    next.bytes = malloc(next.size);
    if (!next.bytes)
        return overlay_error(error, capacity, "could not allocate normalized registry");
    memcpy(next.bytes, "BND1", 4);
    for (unsigned byte = 0; byte < 4; ++byte)
        next.bytes[4 + byte] = (uint8_t)(count >> (8 * byte));
    for (uint32_t i = 0; i < count; ++i)
        memcpy(next.bytes + 8 + (size_t)i * 44, rows[i], 44);
    next.section = (PortableAssetSectionView){.kind = PORTABLE_ASSET_SECTION_MODEL_BINDINGS,
                                              .content_id = 1,
                                              .version = 1,
                                              .bytes = next.bytes,
                                              .size = next.size};
    portable_model_bindings_overlay_close(out);
    *out = next;
    if (error && capacity)
        error[0] = 0;
    return 1;
}

static int ambiguous_key(const uint8_t *rows, uint32_t count, uint32_t index)
{
    for (uint32_t i = 0; i < count; ++i)
        if (i != index && same_key(rows + i * 44, rows + index * 44))
            return 1;
    return 0;
}

static int open_binding(const PortableAssetPackView *pack, const uint8_t *row,
                        PortableModelBinding *out)
{
    memset(out, 0, sizeof(*out));
    out->kind = read_u32(row);
    out->target_id = read_u32(row + 4);
    out->art_bank = read_u32(row + 8);
    out->pose_override = read_u32(row + 12);
    out->mesh_id = read_u32(row + 16);
    if ((out->kind != PORTABLE_MODEL_DRIVER && out->kind != PORTABLE_MODEL_SCENERY &&
         out->kind != PORTABLE_MODEL_ITEM) ||
        !out->target_id || out->pose_override > 31 ||
        (out->kind == PORTABLE_MODEL_SCENERY && out->art_bank >= 64) ||
        (out->kind == PORTABLE_MODEL_ITEM &&
         (out->art_bank >= PORTABLE_MODEL_ITEM_SLOTS || out->pose_override > PORTABLE_MODEL_ITEM_FLAGS)) ||
        (out->kind == PORTABLE_MODEL_DRIVER && out->art_bank != 0))
        return 0;
    for (unsigned i = 0; i < 3; ++i)
    {
        out->scale_q16[i] = read_i32(row + 20 + i * 4);
        out->pivot_q16[i] = read_i32(row + 32 + i * 4);
        if (!out->scale_q16[i])
            return 0;
    }
    return open_mesh(pack, out->mesh_id, &out->mesh);
}

int portable_model_catalog_open(PortableModelCatalog *catalog, const PortableAssetPackView *pack)
{
    if (!catalog || !pack || (!pack->bytes && !pack->resolved_sections))
        return 0;
    PortableModelCatalog next = {0};
    PortableAssetSectionView section;
    PortableAssetPackStatus status =
        portable_asset_pack_find_section(pack, PORTABLE_ASSET_SECTION_MODEL_BINDINGS, 1, &section);
    if (status == PORTABLE_ASSET_PACK_OK)
    {
        if (!section.bytes || section.version != 1 || !optional_flags(section.flags) || section.size < 8 ||
            read_u32(section.bytes) != 0x31444e42u ||
            read_u32(section.bytes + 4) > PORTABLE_MODEL_MAX_BINDINGS ||
            section.size != 8u + (size_t)read_u32(section.bytes + 4) * 44u)
        {
            portable_diagnostic_printf("Models: invalid BND1 registry; using billboard art\n");
        }
        else
        {
            uint32_t count = read_u32(section.bytes + 4);
            const uint8_t *rows = section.bytes + 8;
            if (count)
            {
                next.bindings = calloc(count, sizeof(*next.bindings));
                if (!next.bindings)
                    return 0;
            }
            for (uint32_t i = 0; i < count; ++i)
            {
                const uint8_t *row = rows + i * 44;
                int duplicate = ambiguous_key(rows, count, i);
                if (!duplicate && open_binding(pack, row, &next.bindings[next.count]))
                {
                    ++next.count;
                    continue;
                }
                /* Duplicate keys have no last-wins meaning within one registry. */
                int first = 1;
                for (uint32_t j = 0; duplicate && j < i; ++j)
                    if (same_key(rows + j * 44, row))
                        first = 0;
                if (first)
                    portable_diagnostic_printf(
                        "Models: binding kind=%u target=%u bank=%u pose=%u mesh=%u: %s; using billboard art\n",
                        read_u32(row), read_u32(row + 4), read_u32(row + 8), read_u32(row + 12),
                        read_u32(row + 16),
                        duplicate ? "ambiguous key" : "invalid or missing asset");
            }
        }
    }
    portable_model_catalog_close(catalog);
    *catalog = next;
    return 1;
}

void portable_model_catalog_close(PortableModelCatalog *catalog)
{
    if (catalog)
    {
        free(catalog->bindings);
        memset(catalog, 0, sizeof(*catalog));
    }
}

const PortableModelBinding *portable_model_catalog_find(const PortableModelCatalog *catalog,
                                                        uint32_t kind, uint32_t target_id,
                                                        uint32_t art_bank, uint32_t pose_override)
{
    if (catalog)
        for (size_t i = 0; i < catalog->count; ++i)
        {
            const PortableModelBinding *binding = &catalog->bindings[i];
            if (binding->kind == kind && binding->target_id == target_id &&
                binding->art_bank == art_bank && binding->pose_override == pose_override)
                return binding;
        }
    return NULL;
}

const PortableModelBinding *portable_model_catalog_find_item(const PortableModelCatalog *catalog,
                                                             uint32_t profile_id, uint32_t slot)
{
    if (catalog)
        for (size_t i = 0; i < catalog->count; ++i)
        {
            const PortableModelBinding *binding = &catalog->bindings[i];
            if (binding->kind == PORTABLE_MODEL_ITEM && binding->target_id == profile_id &&
                binding->art_bank == slot)
                return binding;
        }
    return NULL;
}

int portable_model_vertex(const PortableModelView *mesh, size_t index, int32_t xyz_q16[3],
                          int32_t uv_q16[2])
{
    if (!mesh || !mesh->vertices || index >= mesh->vertex_count || !xyz_q16 || !uv_q16)
        return 0;
    const uint8_t *p = mesh->vertices + index * 20;
    for (unsigned i = 0; i < 3; ++i)
        xyz_q16[i] = read_i32(p + i * 4);
    for (unsigned i = 0; i < 2; ++i)
        uv_q16[i] = read_i32(p + 12 + i * 4);
    return 1;
}

int portable_model_triangle(const PortableModelView *mesh, size_t index, uint16_t indices[3],
                            uint16_t *material)
{
    if (!mesh || !mesh->triangles || index >= mesh->triangle_count || !indices || !material)
        return 0;
    const uint8_t *p = mesh->triangles + index * 8;
    for (unsigned i = 0; i < 3; ++i)
        indices[i] = read_u16(p + i * 2);
    *material = read_u16(p + 6);
    return 1;
}

int portable_model_rig_open(const PortableAssetPackView *pack, uint32_t mesh_id,
                            const PortableModelView *mesh, PortableModelRig *out)
{
    PortableAssetSectionView section;
    if (!pack || !mesh || !out || !mesh_id ||
        portable_asset_pack_find_section(pack, PORTABLE_ASSET_SECTION_MODEL_RIG, mesh_id, &section) !=
            PORTABLE_ASSET_PACK_OK)
        return 0;
    const uint8_t *p = section.bytes;
    PortableModelRig rig;
    memset(&rig, 0, sizeof rig);
    const char *problem = NULL;
    if (section.version != 1 || !optional_flags(section.flags) || !p || section.size < 32 || read_u32(p) != 0x31474952u)
        problem = "bad header";
    else
    {
        rig.vertex_count = read_u32(p + 4);
        rig.node_count = read_u32(p + 8);
        rig.clip_count = read_u32(p + 12);
        rig.pose_count = read_u32(p + 16);
        rig.swap_count = read_u32(p + 20);
        const size_t node_bytes = ((size_t)rig.vertex_count + 3u) & ~(size_t)3u;
        if (rig.vertex_count != mesh->vertex_count || !rig.node_count ||
            rig.node_count > PORTABLE_MODEL_RIG_MAX_NODES || rig.clip_count > PORTABLE_MODEL_RIG_MAX_CLIPS ||
            rig.swap_count > PORTABLE_MODEL_RIG_MAX_SWAPS || rig.pose_count > 1u << 20 ||
            section.size != 32u + node_bytes + rig.clip_count * 16u + (size_t)rig.pose_count * 48u +
                                rig.swap_count * 16u)
            problem = "counts do not fit the mesh or section";
        else
        {
            rig.nodes = p + 32;
            for (uint32_t i = 0; !problem && i < rig.vertex_count; ++i)
                if (rig.nodes[i] >= rig.node_count && rig.nodes[i] != PORTABLE_MODEL_RIG_STATIC)
                    problem = "vertex node out of range";
            const uint8_t *clips = rig.nodes + node_bytes;
            for (uint32_t i = 0; !problem && i < rig.clip_count; ++i)
            {
                PortableModelClip *clip = &rig.clips[i];
                clip->tag = read_u32(clips + i * 16u);
                clip->frames = read_u32(clips + i * 16u + 4u);
                clip->first_pose = read_u32(clips + i * 16u + 8u);
                clip->flags = read_u32(clips + i * 16u + 12u);
                if (!clip->frames || (clip->flags & ~(uint32_t)PORTABLE_MODEL_CLIP_LOOP) ||
                    clip->first_pose > rig.pose_count ||
                    (uint64_t)clip->frames * rig.node_count > rig.pose_count - clip->first_pose)
                    problem = "clip outside its poses";
            }
            rig.poses = clips + rig.clip_count * 16u;
            const uint8_t *swaps = rig.poses + (size_t)rig.pose_count * 48u;
            for (uint32_t i = 0; !problem && i < rig.swap_count; ++i)
            {
                PortableModelSwap *swap = &rig.swaps[i];
                swap->slot = read_u32(swaps + i * 16u);
                swap->alternate = read_u32(swaps + i * 16u + 4u);
                swap->period = read_u32(swaps + i * 16u + 8u);
                swap->start = read_u32(swaps + i * 16u + 12u);
                if (swap->slot >= mesh->material_count || swap->alternate >= mesh->material_count ||
                    !swap->start || swap->start >= swap->period)
                    problem = "swap outside the materials or period";
                /* Swaps of one slot form one flipbook: one period, rising starts. */
                for (uint32_t j = 0; !problem && j < i; ++j)
                    if (rig.swaps[j].slot == swap->slot &&
                        (rig.swaps[j].period != swap->period || rig.swaps[j].start >= swap->start))
                        problem = "swaps of one slot need one period and rising starts";
            }
        }
    }
    if (problem)
    {
        portable_diagnostic_printf("Models: rig for mesh %u: %s; drawing it unposed\n", mesh_id, problem);
        return 0;
    }
    *out = rig;
    return 1;
}

int portable_model_rig_clip(const PortableModelRig *rig, uint32_t tag)
{
    for (uint32_t i = 0; rig && i < rig->clip_count; ++i)
        if (rig->clips[i].tag == tag)
            return (int)i;
    return -1;
}

void portable_model_rig_pose(const PortableModelRig *rig, uint32_t clip, uint32_t frame, uint32_t node,
                             int32_t out[12])
{
    const PortableModelClip *c = &rig->clips[clip];
    if (frame >= c->frames)
        frame = c->frames - 1u;
    const uint8_t *row = rig->poses + ((size_t)c->first_pose + (size_t)frame * rig->node_count + node) * 48u;
    for (unsigned i = 0; i < 12; ++i)
        out[i] = read_i32(row + i * 4u);
}

static int driver_3d_row(const PortableAssetSectionView *section, PortableDriver3D *out)
{
    const uint8_t *p = section->bytes;
    if (section->kind != PORTABLE_ASSET_SECTION_DRIVER_3D || section->version != 1 ||
        !optional_flags(section->flags) || !p || section->size != 64 || read_u32(p) != 0x33565244u)
        return 0;
    PortableDriver3D row = {.id = section->content_id, .donor = read_u32(p + 4), .flags = read_u32(p + 8),
                            .mass_q16 = read_i32(p + 12), .speed_q16 = read_i32(p + 16),
                            .acceleration_q16 = read_i32(p + 20), .handling_q16 = read_i32(p + 24),
                            .portrait = read_u32(p + 28)};
    memcpy(row.name, p + 32, sizeof row.name);
    const int32_t ratios[4] = {row.mass_q16, row.speed_q16, row.acceleration_q16, row.handling_q16};
    for (unsigned i = 0; i < 4u; ++i)
        if (ratios[i] <= 0 || ratios[i] > 4 * 65536)
            return 0;
    if (!row.id || row.id > 255u || row.donor < 1u || row.donor > 8u ||
        (row.flags & ~(uint32_t)PORTABLE_DRIVER_3D_SELECTABLE) || !row.name[0] || row.name[31])
        return 0;
    for (unsigned i = 0; row.name[i]; ++i)
        if ((unsigned char)row.name[i] < 0x20u || (unsigned char)row.name[i] > 0x7Eu)
            return 0;
    *out = row;
    return 1;
}

int portable_driver_3d_find(const PortableAssetPackView *pack, uint32_t id, PortableDriver3D *out)
{
    PortableAssetSectionView section;
    PortableDriver3D row;
    if (!pack || !out ||
        portable_asset_pack_find_section(pack, PORTABLE_ASSET_SECTION_DRIVER_3D, id, &section) !=
            PORTABLE_ASSET_PACK_OK ||
        !driver_3d_row(&section, &row))
        return 0;
    *out = row;
    return 1;
}

size_t portable_driver_3d_list(const PortableAssetPackView *pack, PortableDriver3D *out, size_t capacity)
{
    size_t count = 0;
    for (uint32_t i = 0; pack && out && i < pack->section_count; ++i)
    {
        PortableAssetSectionView section;
        PortableDriver3D row;
        if (portable_asset_pack_section_at(pack, i, &section) != PORTABLE_ASSET_PACK_OK ||
            section.kind != PORTABLE_ASSET_SECTION_DRIVER_3D)
            continue;
        if (!driver_3d_row(&section, &row))
        {
            portable_diagnostic_printf("Models: invalid 3D driver row %u; skipped\n", section.content_id);
            continue;
        }
        if (count == capacity)
            break;
        size_t at = count++;
        while (at && out[at - 1].id > row.id)
        {
            out[at] = out[at - 1];
            --at;
        }
        out[at] = row;
    }
    return count;
}

static int kart_3d_row(const PortableAssetSectionView *section, PortableKart3D *out)
{
    const uint8_t *p = section->bytes;
    if (section->kind != PORTABLE_ASSET_SECTION_KART_3D || section->version != 1 ||
        !optional_flags(section->flags) || !p || section->size < 80 || read_u32(p) != 0x3354524Bu)
        return 0;
    PortableKart3D row = {.id = section->content_id, .owner = read_u32(p + 4), .flags = read_u32(p + 8),
                          .mesh_id = read_u32(p + 12), .speed_q16 = read_i32(p + 28),
                          .acceleration_q16 = read_i32(p + 32), .handling_q16 = read_i32(p + 36),
                          .mass_q16 = read_i32(p + 40), .seat_count = read_u32(p + 44)};
    for (unsigned axis = 0; axis < 3u; ++axis)
        row.pivot_q16[axis] = read_i32(p + 16 + axis * 4u);
    memcpy(row.name, p + 48, sizeof row.name);
    const int32_t ratios[4] = {row.mass_q16, row.speed_q16, row.acceleration_q16, row.handling_q16};
    for (unsigned i = 0; i < 4u; ++i)
        if (ratios[i] <= 0 || ratios[i] > 4 * 65536)
            return 0;
    if (!row.id || !row.mesh_id || row.seat_count > PORTABLE_KART_3D_SEATS ||
        section->size != 80u + row.seat_count * 16u ||
        (row.flags & ~(uint32_t)(PORTABLE_KART_3D_SELECTABLE | PORTABLE_KART_3D_OWNER_DEFAULT |
                                  PORTABLE_KART_3D_NO_SHADOW)) ||
        ((row.flags & PORTABLE_KART_3D_OWNER_DEFAULT) && !row.owner) || !row.name[0] || row.name[31])
        return 0;
    for (unsigned i = 0; row.name[i]; ++i)
        if ((unsigned char)row.name[i] < 0x20u || (unsigned char)row.name[i] > 0x7Eu)
            return 0;
    for (uint32_t seat = 0; seat < row.seat_count; ++seat)
    {
        row.seat_driver[seat] = read_u32(p + 80 + seat * 16u);
        if (!row.seat_driver[seat])
            return 0;
        for (unsigned axis = 0; axis < 3u; ++axis)
            row.seat_q16[seat][axis] = read_i32(p + 84 + seat * 16u + axis * 4u);
    }
    *out = row;
    return 1;
}

int portable_kart_3d_find(const PortableAssetPackView *pack, uint32_t id, PortableKart3D *out)
{
    PortableAssetSectionView section;
    PortableKart3D row;
    if (!pack || !out ||
        portable_asset_pack_find_section(pack, PORTABLE_ASSET_SECTION_KART_3D, id, &section) !=
            PORTABLE_ASSET_PACK_OK ||
        !kart_3d_row(&section, &row))
        return 0;
    *out = row;
    return 1;
}

size_t portable_kart_3d_list(const PortableAssetPackView *pack, PortableKart3D *out, size_t capacity)
{
    size_t count = 0;
    for (uint32_t i = 0; pack && out && i < pack->section_count; ++i)
    {
        PortableAssetSectionView section;
        PortableKart3D row;
        if (portable_asset_pack_section_at(pack, i, &section) != PORTABLE_ASSET_PACK_OK ||
            section.kind != PORTABLE_ASSET_SECTION_KART_3D)
            continue;
        if (!kart_3d_row(&section, &row))
        {
            portable_diagnostic_printf("Models: invalid 3D kart row %u; skipped\n", section.content_id);
            continue;
        }
        if (count == capacity)
            break;
        size_t at = count++;
        while (at && out[at - 1].id > row.id)
        {
            out[at] = out[at - 1];
            --at;
        }
        out[at] = row;
    }
    return count;
}

uint32_t portable_kart_3d_default(const PortableAssetPackView *pack, uint32_t driver_id)
{
    for (uint32_t i = 0; pack && driver_id && i < pack->section_count; ++i)
    {
        PortableAssetSectionView section;
        PortableKart3D row;
        if (portable_asset_pack_section_at(pack, i, &section) == PORTABLE_ASSET_PACK_OK &&
            section.kind == PORTABLE_ASSET_SECTION_KART_3D && kart_3d_row(&section, &row) &&
            row.owner == driver_id && (row.flags & PORTABLE_KART_3D_OWNER_DEFAULT))
            return row.id;
    }
    return 0u;
}

int portable_kart_3d_seat(const PortableKart3D *kart, uint32_t driver_id, int32_t out[3])
{
    if (!kart || !kart->seat_count)
        return 0;
    uint32_t chosen = 0;
    const uint32_t wanted[2] = {driver_id, kart->owner};
    for (unsigned pass = 2u; pass-- > 0u;)
        for (uint32_t seat = 0; seat < kart->seat_count; ++seat)
            if (kart->seat_driver[seat] == wanted[pass])
            {
                chosen = seat;
                break;
            }
    for (unsigned axis = 0; axis < 3u; ++axis)
        out[axis] = kart->seat_q16[chosen][axis];
    return 1;
}
