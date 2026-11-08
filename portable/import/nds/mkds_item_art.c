#include "mkds_item_art.h"
#include "mkds_model.h"

#include <stdio.h>
#include <string.h>

/* The race's item models (MainRace.carc Item/ and effect/, the item box set in
 * Main/MapObj.carc and Bullet Bill in KartModelMainA.carc), each converted
 * at course scale and bound as a PORTABLE_MODEL_ITEM row of the DS item
 * rules: the item scene draws them instead of the Super Circuit sprites.
 * Shells, bananas, fake boxes, Bob-ombs and the star are single camera-facing
 * planes in the original; Spiny, Boo and the box are solid models. The
 * HUD icon slots (40 + item) reuse those planes' textures: the DS item
 * window shows the same pictures. */
typedef struct ItemModel
{
    uint32_t slot;
    const char *archive, *stem;
    uint32_t flags; /* PORTABLE_MODEL_ITEM_FLAG_SPIN; billboard comes from the model */
    int animated;   /* stem.nsbca loops as the model's IDLE clip */
} ItemModel;

enum
{
    FLAG_BILLBOARD = 1,
    FLAG_SPIN = 2,
    SLOT_BOX = 32,
    SLOT_BOX_SHARD = 33,
    SLOT_BLAST = 34,
    SLOT_INK = 35,
    MODEL_ITEM = 3
};
#define ICON(item) (40u + (item))
#define ITEM_ARCHIVE "data/MainRace.carc"

static const ItemModel models[] = {
    {0x00u, "data/MainRace.carc", "Item/it_koura_g", 0, 0},
    {0x02u, "data/MainRace.carc", "Item/it_koura_r", 0, 0},
    {0x04u, "data/MainRace.carc", "Item/koura_w", FLAG_SPIN, 0},
    {0x05u, "data/MainRace.carc", "Item/it_banana", 0, 0},
    {0x0Au, "data/MainRace.carc", "Item/it_star", 0, 0},
    {0x0Bu, "data/MainRace.carc", "Item/it_teresa", 0, 0},
    {0x10u, "data/MainRace.carc", "Item/it_f_box", 0, 0},
    {0x11u, "data/MainRace.carc", "Item/it_bomb", 0, 0},
    {0x13u, "data/KartModelMainA.carc", "character/common/KillerItem", 0, 0},
    {0x14u, "data/MainRace.carc", "Item/it_gesso", 0, 0},
    {SLOT_BOX, "data/Main/MapObj.carc", "itembox", 0, 1},
    {SLOT_BOX_SHARD, "data/Main/MapObj.carc", "itembox_hahen", 0, 0},
    {SLOT_BLAST, "data/MainRace.carc", "effect/bombcore/bombcore", 0, 0},
    {SLOT_INK, "data/MainRace.carc", "Item/geso_sumi", 0, 0},
    {ICON(0x00u), ITEM_ARCHIVE, "Item/it_koura_g", 0, 0},  {ICON(0x01u), ITEM_ARCHIVE, "Item/it_koura_g", 0, 0},
    {ICON(0x02u), ITEM_ARCHIVE, "Item/it_koura_r", 0, 0},  {ICON(0x03u), ITEM_ARCHIVE, "Item/it_koura_r", 0, 0},
    {ICON(0x04u), ITEM_ARCHIVE, "Item/it_koura_w", 0, 0},  {ICON(0x05u), ITEM_ARCHIVE, "Item/it_banana", 0, 0},
    {ICON(0x07u), ITEM_ARCHIVE, "Item/it_kinoko", 0, 0},   {ICON(0x08u), ITEM_ARCHIVE, "Item/it_kinoko", 0, 0},
    {ICON(0x09u), ITEM_ARCHIVE, "Item/it_kinoko_p", 0, 0}, {ICON(0x0Au), ITEM_ARCHIVE, "Item/it_star", 0, 0},
    {ICON(0x0Bu), ITEM_ARCHIVE, "Item/it_teresa", 0, 0},   {ICON(0x0Cu), ITEM_ARCHIVE, "Item/it_thunder", 0, 0},
    {ICON(0x0Fu), ITEM_ARCHIVE, "Item/it_kinoko", 0, 0},   {ICON(0x10u), ITEM_ARCHIVE, "Item/it_f_box", 0, 0},
    {ICON(0x11u), ITEM_ARCHIVE, "Item/it_bomb", 0, 0},     {ICON(0x12u), ITEM_ARCHIVE, "Item/it_banana", 0, 0},
    {ICON(0x13u), ITEM_ARCHIVE, "Item/it_killer", 0, 0},   {ICON(0x14u), ITEM_ARCHIVE, "Item/it_gesso", 0, 0},
};

uint32_t mkds_item_art(RiCtx *ctx, const NdsFs *rom, const uint32_t *profiles, size_t profile_count,
                       RiBuf *bindings)
{
    NdsFs archives[4];
    const char *names[4] = {0};
    unsigned archive_count = 0;
    uint32_t rows = 0;
    for (size_t index = 0; index < sizeof(models) / sizeof(models[0]); ++index)
    {
        const ItemModel *item = &models[index];
        /* A stem already converted (an icon of a held item) reuses its mesh. */
        size_t earlier = 0;
        while (earlier < index && (strcmp(models[earlier].archive, item->archive) != 0 ||
                                   strcmp(models[earlier].stem, item->stem) != 0 || models[earlier].animated))
            ++earlier;
        if (earlier < index)
        {
            for (size_t profile = 0; profile < profile_count; ++profile, ++rows)
                ri_words(ctx, bindings, 11, (uint32_t)MODEL_ITEM, profiles[profile], item->slot, item->flags,
                         MKDS_ITEM_MESH_ID(models[earlier].slot), 65536u, 65536u, 65536u, 0u, 0u, 0u);
            continue;
        }
        unsigned which = 0;
        while (which < archive_count && strcmp(names[which], item->archive) != 0)
            ++which;
        if (which == archive_count)
        {
            archives[which] = nds_narc(ctx, nds_file(ctx, rom, item->archive), item->archive);
            names[which] = item->archive;
            ++archive_count;
        }
        char where[96];
        snprintf(where, sizeof(where), "Mario Kart DS item model %s/%s", item->archive, item->stem);
        MkdsModel scratch;
        mkds_model_init(&scratch, ctx, where, 0, NULL, 0, 15);
        MkdsResources model;
        if (!mkds_resources(&scratch, &archives[which], NULL, item->stem, &model))
            ri_fail(ctx, "%s: %s.nsbmd is missing", where, item->stem);
        NitroJointAnimation animation;
        if (item->animated)
        {
            char path[96], label[160];
            snprintf(path, sizeof(path), "%s.nsbca", item->stem);
            snprintf(label, sizeof(label), "%s/%s", item->archive, path);
            animation = nitro_bca(ctx, nds_file(ctx, &archives[which], path),
                                  (const char *)ri_buf_copy(ctx, label, strlen(label) + 1)->data);
        }
        const MkdsObjectMesh mesh = mkds_object_mesh(ctx, where, &model, item->animated ? &animation : NULL,
                                                     MKDS_ITEM_MESH_ID(item->slot),
                                                     MKDS_ITEM_MATERIAL_ID(item->slot, 0u), 1);
        const uint32_t flags = item->flags | (mesh.billboard ? FLAG_BILLBOARD : 0u);
        /* The box rests on its spawn point: its pivot is its lowest vertex. */
        int32_t lowest = 0;
        for (uint32_t vertex = 0; item->slot == SLOT_BOX && vertex < mesh.vertex_count; ++vertex)
        {
            const int32_t z = (int32_t)ri_le32(mesh.vertices + vertex * 20u + 8u);
            lowest = vertex == 0 || z < lowest ? z : lowest;
        }
        for (size_t profile = 0; profile < profile_count; ++profile, ++rows)
            ri_words(ctx, bindings, 11, (uint32_t)MODEL_ITEM, profiles[profile], item->slot, flags, mesh.id, 65536u,
                     65536u, 65536u, 0u, 0u, (uint32_t)lowest);
    }
    return rows;
}
