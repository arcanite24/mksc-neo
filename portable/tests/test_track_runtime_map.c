#include "track_runtime_map.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    uint8_t blob[32] = {0};
    uint8_t map[16] = {0};
    uint8_t tiles[PORTABLE_TRACK_RUNTIME_DYNAMIC_LIMIT * 64u] = {0};
    uint8_t source_flags[256] = {0};
    uint8_t runtime_flags[256] = {0};
    PortableTrackObjectStream stream = {
        blob, sizeof(blob), 0u, 4u, 16u, 3u,
    };
    PortableTrackMaterialView material = {
        .flags=source_flags, .count=sizeof(source_flags),
    };
    PortableTrackRuntimeMapReport report;

    /* Three source objects: tile 7 is reused and tile 9 allocates the next ID. */
    blob[4] = 2u; blob[5] = 1u; blob[6] = 1u;
    blob[8] = 2u; blob[9] = 2u; blob[10] = 1u;
    blob[12] = 2u; blob[13] = 3u; blob[14] = 2u;
    map[5] = 7u; map[6] = 9u; map[11] = 7u;
    memset(tiles + 7u * 64u, 0x17, 64u);
    memset(tiles + 9u * 64u, 0x29, 64u);
    source_flags[7] = 0x47u;
    source_flags[9] = 0x59u;
    source_flags[0xd0u] = 0x90u;
    source_flags[0xd1u] = 0x91u;

    if (portable_track_runtime_map_apply_object_stream(
            &stream, map, sizeof(map), 4u, 4u, tiles, sizeof(tiles),
            &material, runtime_flags, sizeof(runtime_flags), &report) !=
            PORTABLE_TRACK_RUNTIME_MAP_OK ||
        report.mutation_count != 3u || report.dynamic_tile_count != 2u ||
        map[5] != 0xd0u || map[6] != 0xd1u || map[11] != 0xd0u ||
        report.mutations[0].source_tile != 7u ||
        report.mutations[2].dynamic_tile != 0xd0u ||
        tiles[0xd0u * 64u] != 0x17u || tiles[0xd1u * 64u] != 0x29u ||
        runtime_flags[0xd0u] != 0x90u || runtime_flags[0xd1u] != 0x91u ||
        runtime_flags[7] != 0x47u)
    {
        fputs("runtime map mutation mismatch\n", stderr);
        return 1;
    }
    puts("portable track runtime map: PASS");
    return 0;
}
