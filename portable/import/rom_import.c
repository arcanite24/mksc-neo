#include "rom_import_internal.h"
#include "rom_import_frontend.h"

void ri_group_stamp(RiCtx *ctx)
{
    RiBuf *stamp = ri_buf(ctx);
    ri_words(ctx, stamp, 2, 0x31504D49u, MKSC_ROM_IMPORT_VERSION);
    ri_section(ctx, 80, 1, 1, stamp);
}

static const RiGroup groups[] = {
    ri_group_drivers, ri_group_overlays, ri_group_courses, ri_group_audio, ri_group_frontend,
    ri_group_stamp,
};

int mksc_rom_import(const uint8_t *rom, size_t size, MkscRomImportResult *result,
                    char *error, size_t error_size)
{
    return ri_build(rom, size, 1, groups, sizeof(groups) / sizeof(groups[0]), result, error,
                    error_size);
}

int mksc_rom_import_title_presentation(const uint8_t *rom, size_t size,
    MkscRomImportResult *result, char *error, size_t error_size)
{
    const RiGroup title_groups[] = {ri_fe_title_presentation_sections};
    return ri_build(rom, size, 1, title_groups,
        sizeof(title_groups) / sizeof(title_groups[0]), result, error, error_size);
}
