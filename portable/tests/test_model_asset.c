#include "model_asset.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static void word(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        p[i] = (uint8_t)(value >> (i * 8));
}

static void binding(uint8_t *p, uint32_t target, uint32_t pose, uint32_t mesh)
{
    memset(p, 0, 44);
    word(p, PORTABLE_MODEL_DRIVER);
    word(p + 4, target);
    word(p + 12, pose);
    word(p + 16, mesh);
    word(p + 20, 65536);
    word(p + 24, 131072);
    word(p + 28, 65536);
    word(p + 32, UINT32_MAX); /* Signed decoding must not depend on host casts. */
}

static PortableAssetSectionView registry_section(uint8_t *bytes, uint32_t count, uint32_t version)
{
    word(bytes, 0x31444e42);
    word(bytes + 4, count);
    return (PortableAssetSectionView){.kind = PORTABLE_ASSET_SECTION_MODEL_BINDINGS,
                                      .content_id = 1,
                                      .version = version,
                                      .bytes = bytes,
                                      .size = 8u + (size_t)count * 44u};
}
static int overlay_tests(const PortableAssetPackView *assets)
{
    uint8_t base[8 + 256 * 44] = {0}, patch[8 + 256 * 44] = {0};
    PortableAssetSectionView previous = registry_section(base, 2, 1);
    PortableAssetSectionView overlay = registry_section(patch, 1, 2);
    binding(base + 8, 256, 0, 7);
    binding(base + 52, 2, 0, 7);
    binding(patch + 8, 128, 3, 7);
    PortableModelBindingOverlay owner = {0};
    char error[128];
    CHECK(
        portable_model_bindings_overlay_prepare(&previous, &overlay, &owner, error, sizeof error));
    CHECK(!error[0] && owner.size == 8 + 3 * 44 && owner.section.version == 1 &&
          owner.section.flags == 0 && owner.section.bytes == owner.bytes &&
          owner.section.size == owner.size);
    CHECK(!memcmp(owner.bytes + 8, base + 52, 44) && !memcmp(owner.bytes + 52, patch + 8, 44) &&
          !memcmp(owner.bytes + 96, base + 8, 44)); /* Numeric key sort, not LE byte sort. */
    PortableAssetSectionView sections[3];
    memcpy(sections, assets->resolved_sections, sizeof sections);
    sections[2] = owner.section;
    PortableAssetPackView pack = {.section_count = 3, .resolved_sections = sections};
    PortableModelCatalog catalog = {0};
    CHECK(portable_model_catalog_open(&catalog, &pack) && catalog.count == 3);
    /* Invalid winner masks the older valid row, while unrelated keys survive. */
    binding(patch + 8, 2, 0, 999);
    CHECK(portable_model_bindings_overlay_prepare(&owner.section, &overlay, &owner, error,
                                                  sizeof error));
    sections[2] = owner.section;
    CHECK(portable_model_catalog_open(&catalog, &pack) && catalog.count == 2);
    CHECK(!portable_model_catalog_find(&catalog, 1, 2, 0, 0));
    /* Duplicate incoming rows replace ALL old rows and stay ambiguous locally. */
    overlay = registry_section(patch, 2, 2);
    binding(patch + 8, 256, 0, 7);
    binding(patch + 52, 256, 0, 999);
    CHECK(portable_model_bindings_overlay_prepare(&owner.section, &overlay, &owner, error,
                                                  sizeof error));
    CHECK(owner.size == 8 + 4 * 44 && !memcmp(owner.bytes + 96, patch + 8, 44) &&
          !memcmp(owner.bytes + 140, patch + 52, 44));
    sections[2] = owner.section;
    CHECK(portable_model_catalog_open(&catalog, &pack) && catalog.count == 1);
    CHECK(!portable_model_catalog_find(&catalog, 1, 256, 0, 0));
    /* A later single row repairs duplicate ambiguity, never leaves a stale twin. */
    overlay = registry_section(patch, 1, 2);
    CHECK(portable_model_bindings_overlay_prepare(&owner.section, &overlay, &owner, error,
                                                  sizeof error));
    CHECK(owner.size == 8 + 3 * 44);
    sections[2] = owner.section;
    CHECK(portable_model_catalog_open(&catalog, &pack) && catalog.count == 2);
    CHECK(portable_model_catalog_find(&catalog, 1, 256, 0, 0));
    /* v1 malformed envelopes inherit as empty, matching legacy fallback. */
    previous.size = 7;
    CHECK(
        portable_model_bindings_overlay_prepare(&previous, &overlay, &owner, error, sizeof error));
    CHECK(owner.size == 52 && !memcmp(owner.bytes + 8, patch + 8, 44));
    previous.size = 96;
    word(base, 0);
    CHECK(
        portable_model_bindings_overlay_prepare(&previous, &overlay, &owner, error, sizeof error));
    CHECK(owner.size == 52);
    /* Raw invalid keys/transforms are not filtered by normalization. */
    memset(patch + 8, 0, 44);
    CHECK(portable_model_bindings_overlay_prepare(NULL, &overlay, &owner, error, sizeof error));
    CHECK(!memcmp(owner.bytes + 8, patch + 8, 44));
    /* The cap counts raw duplicates too; replacing a full registry is valid. */
    overlay = registry_section(patch, 256, 2);
    for (unsigned i = 0; i < 256; ++i)
        binding(patch + 8 + i * 44, 1, 0, i + 1);
    CHECK(portable_model_bindings_overlay_prepare(NULL, &overlay, &owner, error, sizeof error));
    CHECK(owner.size == sizeof patch); /* Ambiguous rows still consume capacity. */
    for (unsigned i = 0; i < 256; ++i)
        binding(patch + 8 + i * 44, 1 + i, 0, 7);
    CHECK(portable_model_bindings_overlay_prepare(NULL, &overlay, &owner, error, sizeof error));
    CHECK(owner.size == sizeof patch);
    binding(patch + 8, 1, 0, 999);
    overlay.size = 52;
    word(patch + 4, 1);
    CHECK(portable_model_bindings_overlay_prepare(&owner.section, &overlay, &owner, error,
                                                  sizeof error));
    CHECK(owner.size == sizeof patch && !memcmp(owner.bytes + 8, patch + 8, 44));
    /* All failure paths preserve the previous heap owner and its payload. */
    uint8_t saved_bytes[sizeof patch];
    memcpy(saved_bytes, owner.bytes, owner.size);
    const PortableModelBindingOverlay saved = owner;
    binding(patch + 8, 257, 0, 7);
    CHECK(!portable_model_bindings_overlay_prepare(&owner.section, &overlay, &owner, error,
                                                   sizeof error));
    CHECK(strstr(error, "256 raw rows") && !memcmp(&owner, &saved, sizeof owner) &&
          !memcmp(owner.bytes, saved_bytes, owner.size));
    for (unsigned bad = 0; bad < 9; ++bad)
    {
        PortableAssetSectionView invalid = overlay;
        switch (bad)
        {
        case 0:
            invalid.kind = 97;
            break;
        case 1:
            invalid.content_id = 2;
            break;
        case 2:
            invalid.version = 1;
            break;
        case 3:
            invalid.flags = 1;
            break;
        case 4:
            invalid.bytes = NULL;
            break;
        case 5:
            invalid.size = 7;
            break;
        case 6:
            invalid.size = 53;
            break;
        case 7:
            word(patch, 0);
            break;
        default:
            word(patch + 4, 257);
            break;
        }
        CHECK(
            !portable_model_bindings_overlay_prepare(NULL, &invalid, &owner, error, sizeof error));
        CHECK(strstr(error, "BND1 overlay") && !memcmp(&owner, &saved, sizeof owner) &&
              !memcmp(owner.bytes, saved_bytes, owner.size));
        word(patch, 0x31444e42);
        word(patch + 4, 1);
    }
    previous = owner.section;
    previous.version = 2;
    CHECK(
        !portable_model_bindings_overlay_prepare(&previous, &overlay, &owner, error, sizeof error));
    CHECK(strstr(error, "predecessor") && !memcmp(&owner, &saved, sizeof owner));
    CHECK(!portable_model_bindings_overlay_prepare(NULL, NULL, &owner, error, sizeof error));
    CHECK(!portable_model_bindings_overlay_prepare(NULL, &overlay, NULL, error, sizeof error));
    CHECK(!memcmp(&owner, &saved, sizeof owner) && !memcmp(owner.bytes, saved_bytes, owner.size));
    /* Empty overlays retain inherited rows and can also normalize empty packs. */
    overlay = registry_section(patch, 0, 2);
    CHECK(portable_model_bindings_overlay_prepare(&owner.section, &overlay, &owner, error,
                                                  sizeof error));
    CHECK(owner.size == sizeof patch && !memcmp(owner.bytes, saved_bytes, owner.size));
    CHECK(portable_model_bindings_overlay_prepare(NULL, &overlay, &owner, error, sizeof error));
    CHECK(owner.size == 8);
    portable_model_catalog_close(&catalog);
    portable_model_bindings_overlay_close(&owner);
    portable_model_bindings_overlay_close(&owner);
    CHECK(!owner.bytes && !owner.size && !owner.section.bytes);
    return 0;
}

int main(void)
{
    uint8_t mesh[88] = {0}, material[52] = {0}, registry[8 + 4 * 44] = {0};
    word(mesh, 0x3148534d);
    word(mesh + 4, 3);
    word(mesh + 8, 1);
    word(mesh + 12, 1);
    word(mesh + 16, 9);
    word(mesh + 20, UINT32_MAX);
    word(mesh + 32, 32768);
    mesh[82] = 1;
    mesh[84] = 2;
    word(material, 0x314c544d);
    word(material + 4, 0x80abcdef);
    word(material + 8, PORTABLE_MODEL_WRAP_U);
    word(material + 12, 36);
    memcpy(material + 16, "MKSCIMG1", 8);
    material[24] = 32;
    material[26] = 1;
    material[28] = material[30] = 1;
    word(material + 40, 32);
    word(material + 44, 36);
    word(material + 48, 0x7f102030);
    word(registry, 0x31444e42);
    word(registry + 4, 2);
    binding(registry + 8, 16, 0, 7);
    binding(registry + 52, 128, 3, 7);
    PortableAssetSectionView sections[3] = {
        {.kind = PORTABLE_ASSET_SECTION_MODEL_MESH,
         .content_id = 7,
         .version = 1,
         .bytes = mesh,
         .size = sizeof(mesh)},
        {.kind = PORTABLE_ASSET_SECTION_MODEL_MATERIAL,
         .content_id = 9,
         .version = 1,
         .bytes = material,
         .size = sizeof(material)},
        {.kind = PORTABLE_ASSET_SECTION_MODEL_BINDINGS,
         .content_id = 1,
         .version = 1,
         .bytes = registry,
         .size = 96},
    };
    PortableAssetPackView pack = {.section_count = 3, .resolved_sections = sections};
    CHECK(overlay_tests(&pack) == 0);
    PortableModelCatalog catalog = {0};
    CHECK(portable_model_catalog_open(&catalog, &pack));
    CHECK(catalog.count == 2);
    const PortableModelBinding *found = portable_model_catalog_find(&catalog, 1, 16, 0, 0);
    CHECK(found && found->pivot_q16[0] == -1 && found->scale_q16[1] == 131072);
    CHECK(found->mesh.vertices == mesh + 20 &&
          found->mesh.materials[0].texture.pixels == material + 48);
    CHECK(found->mesh.materials[0].argb == 0x80abcdef && found->mesh.materials[0].flags == 1);
    CHECK(!portable_model_catalog_find(&catalog, 1, 128, 0, 0));
    CHECK(portable_model_catalog_find(&catalog, 1, 128, 0, 3));
    int32_t xyz[3], uv[2];
    uint16_t indices[3], slot;
    CHECK(portable_model_vertex(&found->mesh, 0, xyz, uv));
    CHECK(xyz[0] == -1 && uv[0] == 32768);
    CHECK(portable_model_triangle(&found->mesh, 0, indices, &slot));
    CHECK(indices[0] == 0 && indices[1] == 1 && indices[2] == 2 && slot == 0);
    CHECK(!portable_model_vertex(&found->mesh, 3, xyz, uv));
    CHECK(!portable_model_triangle(&found->mesh, 1, indices, &slot));
    PortableModelBinding *previous = catalog.bindings;
    CHECK(!portable_model_catalog_open(&catalog, NULL) && catalog.bindings == previous &&
          catalog.count == 2);

    /* One bad row is isolated; ambiguous keys remove both rows, never last-wins. */
    word(registry + 4, 4);
    sections[2].size = sizeof(registry);
    binding(registry + 96, 16, 0, 7);
    binding(registry + 140, 200, 0, 999);
    CHECK(portable_model_catalog_open(&catalog, &pack) && catalog.count == 1);
    CHECK(!portable_model_catalog_find(&catalog, 1, 16, 0, 0));
    CHECK(portable_model_catalog_find(&catalog, 1, 128, 0, 3));

    /* Class/menu index zero is not a manifest content identity. */
    word(registry + 4, 2);
    sections[2].size = 96;
    binding(registry + 8, 0, 0, 7);
    binding(registry + 52, 0, 0, 7);
    word(registry + 52, PORTABLE_MODEL_SCENERY);
    CHECK(portable_model_catalog_open(&catalog, &pack) && catalog.count == 0);
    CHECK(!portable_model_catalog_find(&catalog, 1, 0, 0, 0));
    CHECK(!portable_model_catalog_find(&catalog, 2, 0, 0, 0));
    word(registry + 12, 16);
    word(registry + 56, 128);
    word(registry + 60, 64);
    CHECK(portable_model_catalog_open(&catalog, &pack) && catalog.count == 1);

    /* Exact lengths, material flags, indices, IMG1-only and extent bounds. */
    word(registry + 4, 1);
    sections[2].size = 52;
    sections[0].size--;
    CHECK(portable_model_catalog_open(&catalog, &pack) && catalog.count == 0);
    sections[0].size++;
    mesh[86] = 1;
    CHECK(portable_model_catalog_open(&catalog, &pack) && catalog.count == 0);
    mesh[86] = 0;
    mesh[84] = 3;
    CHECK(portable_model_catalog_open(&catalog, &pack) && catalog.count == 0);
    mesh[84] = 2;
    word(material + 8, 16);
    CHECK(portable_model_catalog_open(&catalog, &pack) && catalog.count == 0);
    word(material + 8, 1);
    material[23] = '2';
    CHECK(portable_model_catalog_open(&catalog, &pack) && catalog.count == 0);
    material[23] = '1';
    material[29] = 8; /* Width2049, outside model cap (also inconsistent image length). */
    CHECK(portable_model_catalog_open(&catalog, &pack) && catalog.count == 0);
    material[29] = 0;
    word(material + 12, 0);
    sections[1].size = 16;
    CHECK(portable_model_catalog_open(&catalog, &pack) && catalog.count == 1);
    CHECK(!catalog.bindings[0].mesh.materials[0].texture.pixels);
    word(registry + 4, PORTABLE_MODEL_MAX_BINDINGS + 1);
    CHECK(portable_model_catalog_open(&catalog, &pack) && catalog.count == 0);
    pack.section_count = 2;
    CHECK(portable_model_catalog_open(&catalog, &pack) && catalog.count == 0);
    portable_model_catalog_close(&catalog);
    portable_model_catalog_close(&catalog);
    CHECK(!catalog.bindings && !catalog.count);
    puts("Model assets: PASS");
    return 0;
}
