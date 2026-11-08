#include "test_support.h"

int run_track_y(void)
{
    uint8_t track[0x400] = {0};
    PortableRaceView race = {
        .track_data = track,
        .nr_checkpoints = 47,
    };
    const size_t track_base = 0x100;
    const size_t table_base = track_base + 0x20;
    const size_t captured_record = table_base + (46u << 3);
    const size_t row_one_record = table_base + ((47u + 0u) << 3);

    track[0xCC] = 0x00;
    track[0xCD] = 0x01;
    track[track_base + 3] = 0x20;
    track[track_base + 4] = 0x00;
    track[captured_record + 2] = 0x45;
    track[captured_record + 3] = 0x00;
    track[row_one_record + 2] = 0xCD;
    track[row_one_record + 3] = 0xAB;

    if (portable_track_read_y(&race, 0xAE, 0) != 0x45)
        return 1;
    if (portable_track_read_y(&race, 0x2E, 0) != 0x45)
        return 1;
    if (portable_track_read_y(&race, 0, 1) != 0xABCD)
        return 1;

    puts("portable track y: PASS");
    return 0;
}

int run_track_import(void)
{
    uint8_t container[0x300] = {0};
    uint8_t owned[0x100] = {0};
    PortableTrackAsset asset;
    PortableTrackCheckpoint checkpoint;
    PortableTrackGeometryView geometry;
    PortableRaceView race;
    const size_t directory_size =
        (size_t)PORTABLE_TRACK_DIRECTORY_ENTRIES * sizeof(uint32_t);
    const size_t blob_offset = 0x100;
    const size_t blob_end = 0x200;
    const size_t track_base = 0x10;
    const size_t table_offset = 0x08;
    const size_t record_offset = table_offset + (1u << 3);
    size_t entry;

    for (entry = 0; entry < PORTABLE_TRACK_DIRECTORY_ENTRIES; ++entry)
        write_le32(container + entry * sizeof(uint32_t),
                   (uint32_t)(entry < 25u ? blob_offset : blob_end));
    write_le32(container + 24u * sizeof(uint32_t), (uint32_t)blob_offset);
    write_le32(container + 25u * sizeof(uint32_t), (uint32_t)blob_end);

    /* The small synthetic blob uses a displacement that fits its fixture. */
    container[blob_offset + 0xCC] = (uint8_t)track_base;
    container[blob_offset + 0xCD] = 0;
    container[blob_offset + 4] = 2;
    container[blob_offset + track_base + 3] = (uint8_t)table_offset;
    container[blob_offset + track_base + 4] = 0;
    container[blob_offset + track_base + record_offset] = 0x34;
    container[blob_offset + track_base + record_offset + 1] = 0x12;
    container[blob_offset + track_base + record_offset + 2] = 0x45;
    container[blob_offset + track_base + record_offset + 3] = 0;
    container[blob_offset + track_base + record_offset + 4] = 2;

    if (portable_track_asset_import(container, sizeof(container), 4, 24,
                                    owned, sizeof(owned), &asset) !=
        PORTABLE_TRACK_ASSET_OK)
        return 1;
    if (asset.data != owned || asset.size != blob_end - blob_offset ||
        asset.header_index != 4 || asset.course_index != 24 ||
        asset.source_offset != blob_offset || asset.source_end != blob_end)
        return 1;

    race.track_data = asset.data;
    race.nr_checkpoints = 2;
    if (portable_track_read_y(&race, 0x81, 0) != 0x45)
        return 1;
    if (portable_track_geometry_init(&asset, 2, &geometry) !=
        PORTABLE_TRACK_GEOMETRY_OK)
        return 1;
    if (geometry.grid_width != 2 ||
        portable_track_geometry_read_checkpoint(&geometry, 0x81, 0,
                                                &checkpoint) !=
            PORTABLE_TRACK_GEOMETRY_OK ||
        checkpoint.x != 0x1234 || checkpoint.y != 0x45 ||
        checkpoint.ai_mode != 2u)
        return 1;
    geometry.nr_checkpoints = 47;
    if (portable_track_geometry_read_checkpoint(&geometry, 0, 1,
                                                &checkpoint) !=
        PORTABLE_TRACK_GEOMETRY_OUT_OF_BOUNDS)
        return 1;
    {
        PortableTrackAsset short_asset = asset;

        short_asset.size = 0xCF;
        if (portable_track_geometry_init(&short_asset, 2, &geometry) !=
            PORTABLE_TRACK_GEOMETRY_INVALID_HEADER)
            return 1;
    }
    if (portable_track_asset_import(container, sizeof(container), 4, 49,
                                    owned, sizeof(owned), &asset) !=
        PORTABLE_TRACK_ASSET_INVALID_ARGUMENT)
        return 1;
    if (portable_track_asset_import(container, sizeof(container), 4, 24,
                                    owned, 0, &asset) !=
        PORTABLE_TRACK_ASSET_DESTINATION_TOO_SMALL)
        return 1;
    write_le32(container + 25u * sizeof(uint32_t), 0x80);
    if (portable_track_asset_import(container, sizeof(container), 4, 24,
                                    owned, sizeof(owned), &asset) !=
        PORTABLE_TRACK_ASSET_INVALID_DIRECTORY)
        return 1;
    write_le32(container + 25u * sizeof(uint32_t), (uint32_t)blob_end);
    if (portable_track_asset_import(container, directory_size - 1u, 4, 24,
                                    owned, sizeof(owned), &asset) !=
        PORTABLE_TRACK_ASSET_CONTAINER_TOO_SMALL)
        return 1;

    puts("portable track import: PASS");
    return 0;
}

int run_track_object_asset(void)
{
    uint8_t data[0x140] = {0};
    PortableTrackAsset asset = {
        .data = data,
        .size = sizeof(data),
    };
    PortableTrackObjectStream coins;
    PortableTrackObjectStream objects;
    PortableTrackObjectStream secondary;
    PortableTrackObjectStream boundaries;
    PortableTrackCoinPlacement coin;
    PortableTrackRawObject object;
    PortableTrackBoundaryCommand boundary;
    uint32_t width;

    write_le32(data + PORTABLE_TRACK_STREAM_COIN, 0x100);
    data[0x100] = 0x80;
    data[0x101] = 0x3c;
    data[0x102] = 0x40;
    data[0x103] = 0x0a;
    data[0x104] = 0x80;
    data[0x105] = 0x3d;
    data[0x106] = 0x41;
    data[0x107] = 0x0a;
    memset(data + 0x108, 0, 4);

    write_le32(data + PORTABLE_TRACK_STREAM_OBJECT, 0x110);
    data[0x110] = 0x02;
    data[0x111] = 0x75;
    data[0x112] = 0x31;
    data[0x113] = 0x03;
    memset(data + 0x114, 0, 4);

    write_le32(data + PORTABLE_TRACK_STREAM_SECONDARY_OBJECT, 0x118);
    data[0x118] = 0x01;
    data[0x119] = 0x3d;
    data[0x11a] = 0x39;
    data[0x11b] = 0x09;
    memset(data + 0x11c, 0, 4);

    write_le32(data + PORTABLE_TRACK_STREAM_BOUNDARY, 0x120);
    data[0x120] = 0x8b;
    data[0x121] = 0x6a;
    data[0x122] = 0x49;
    data[0x123] = 0x01;
    data[0x124] = 0x88;
    data[0x125] = 0x71;
    data[0x126] = 0x62;
    data[0x127] = 0x2d;
    memset(data + 0x128, 0, 4);

    if (portable_track_object_stream_open(
            &asset, PORTABLE_TRACK_STREAM_COIN, &coins) !=
            PORTABLE_TRACK_OBJECT_ASSET_OK ||
        coins.record_count != 2 || coins.terminator_offset != 0x108 ||
        portable_track_coin_read(&coins, 0, &coin) !=
            PORTABLE_TRACK_OBJECT_ASSET_OK ||
        coin.stream_offset != 0x100 || coin.marker != 0x80 ||
        coin.x != 0x3c || coin.y != 0x40 || coin.parameter != 0x0a ||
        coin.world_x != 0x1e0 || coin.world_y != 0x200 ||
        coin.source_flags != PORTABLE_TRACK_SOURCE_COIN_FLAGS)
    {
        return 1;
    }
    if (portable_track_coin_read(&coins, 2, &coin) !=
        PORTABLE_TRACK_OBJECT_ASSET_INDEX_OUT_OF_BOUNDS)
    {
        return 1;
    }

    if (portable_track_object_stream_open(
            &asset, PORTABLE_TRACK_STREAM_OBJECT, &objects) !=
            PORTABLE_TRACK_OBJECT_ASSET_OK ||
        objects.record_count != 1 ||
        portable_track_raw_object_read(&objects, 0, &object) !=
            PORTABLE_TRACK_OBJECT_ASSET_OK ||
        object.raw_type != 2 || object.x != 0x75 || object.y != 0x31 ||
        object.parameter != 3 || object.source_x_q19 != 0x3ac0000u ||
        object.source_y_q19 != 0x18c0000u)
    {
        return 1;
    }

    if (portable_track_object_stream_open(
            &asset, PORTABLE_TRACK_STREAM_SECONDARY_OBJECT, &secondary) !=
            PORTABLE_TRACK_OBJECT_ASSET_OK ||
        secondary.record_count != 1)
    {
        return 1;
    }

    if (portable_track_object_stream_open(
            &asset, PORTABLE_TRACK_STREAM_BOUNDARY, &boundaries) !=
            PORTABLE_TRACK_OBJECT_ASSET_OK ||
        boundaries.record_count != 2 ||
        portable_track_boundary_read(&boundaries, 0, &boundary) !=
            PORTABLE_TRACK_OBJECT_ASSET_OK ||
        boundary.stream_offset != 0x120 || boundary.opcode != 0x8b ||
        boundary.x != 0x6a || boundary.y != 0x49 ||
        boundary.parameter != 1 ||
        portable_track_boundary_width(&boundaries, &width) !=
            PORTABLE_TRACK_OBJECT_ASSET_OK || width != 0x250)
    {
        return 1;
    }

    memset(data + 0x108, 1, sizeof(data) - 0x108);
    if (portable_track_object_stream_open(
            &asset, PORTABLE_TRACK_STREAM_COIN, &coins) !=
        PORTABLE_TRACK_OBJECT_ASSET_TRUNCATED_STREAM)
        return 1;

    puts("portable track object asset: PASS");
    return 0;
}

int run_track_object_art(void)
{
    static uint8_t rom[0x100000];
    static uint8_t tiles[PORTABLE_TRACK_OBJECT_ART_TILE_BYTES];
    static uint16_t palette[PORTABLE_TRACK_OBJECT_ART_PALETTE_COUNT];
    static uint32_t pixels[64 * 64];
    PortableTrackObjectArtBuffers buffers = {
        .tiles = tiles,
        .tile_capacity = sizeof(tiles),
        .palette = palette,
        .palette_capacity = sizeof(palette) / sizeof(palette[0]),
    };
    PortableTrackObjectArtView art;
    PortableSoftwareFrame frame;

    memset(rom, 0, sizeof(rom));
    memset(tiles, 0, sizeof(tiles));
    memset(palette, 0, sizeof(palette));
    write_le32(rom + PORTABLE_TRACK_OBJECT_ART_DESCRIPTOR_OFFSET,
               UINT32_C(0x0807ba38));
    write_le32(rom + PORTABLE_TRACK_OBJECT_ART_DESCRIPTOR_OFFSET + 8u,
               UINT32_C(0x0807ba38));
    write_le32(rom + PORTABLE_TRACK_OBJECT_ART_DESCRIPTOR_OFFSET + 16u,
               UINT32_C(0x0807ba38));
    write_le32(rom + PORTABLE_TRACK_OBJECT_ART_DESCRIPTOR_OFFSET + 24u,
               UINT32_C(0x0807ba40));
    rom[PORTABLE_TRACK_OBJECT_ART_DESCRIPTOR_OFFSET + 4u] = 1;
    rom[PORTABLE_TRACK_OBJECT_ART_DESCRIPTOR_OFFSET + 12u] = 1;
    rom[PORTABLE_TRACK_OBJECT_ART_DESCRIPTOR_OFFSET + 20u] = 1;
    rom[PORTABLE_TRACK_OBJECT_ART_DESCRIPTOR_OFFSET + 28u] = 2;
    write_le16(rom + PORTABLE_TRACK_OBJECT_ART_CELL_OFFSET, 1);
    write_le16(rom + PORTABLE_TRACK_OBJECT_ART_CELL_OFFSET + 2u, 0x00e1);
    write_le16(rom + PORTABLE_TRACK_OBJECT_ART_CELL_OFFSET + 4u, 0x81f0);
    write_le16(rom + PORTABLE_TRACK_OBJECT_ART_CELL_OFFSET + 6u, 0x1480);
    if (build_literal_lz77(
            rom + PORTABLE_TRACK_OBJECT_ART_PRIMARY_STREAM_OFFSET,
            sizeof(rom) - PORTABLE_TRACK_OBJECT_ART_PRIMARY_STREAM_OFFSET,
            PORTABLE_TRACK_OBJECT_ART_PRIMARY_TILE_BYTES, 0x11) == 0 ||
        build_literal_lz77(
            rom + PORTABLE_TRACK_OBJECT_ART_SECONDARY_STREAM_OFFSET,
            sizeof(rom) - PORTABLE_TRACK_OBJECT_ART_SECONDARY_STREAM_OFFSET,
            PORTABLE_TRACK_OBJECT_ART_SECONDARY_TILE_BYTES, 0x22) == 0)
        return 1;
    write_le16(rom + PORTABLE_TRACK_OBJECT_ART_PALETTE_OFFSET + 18u * 2u,
               0x001f);

    if (portable_track_object_art_build(rom, sizeof(rom), &buffers, &art) !=
            PORTABLE_TRACK_OBJECT_ART_OK ||
        art.cell_offset != PORTABLE_TRACK_OBJECT_ART_CELL_OFFSET ||
        art.level3_cell_offset != PORTABLE_TRACK_OBJECT_ART_CELL_OFFSET + 8u ||
        art.level0_shift != 1 || art.level3_shift != 2 ||
        art.primary_decoded_bytes !=
            PORTABLE_TRACK_OBJECT_ART_PRIMARY_TILE_BYTES ||
        art.secondary_decoded_bytes !=
            PORTABLE_TRACK_OBJECT_ART_SECONDARY_TILE_BYTES ||
        tiles[0] != 0x11 ||
        tiles[PORTABLE_TRACK_OBJECT_ART_SECONDARY_TILE_BASE] != 0x22 ||
        art.palette[18] != 0x001f)
        return 1;
    if (!portable_software_frame_init(&frame, pixels, 64, 64))
        return 1;
    portable_software_frame_clear(&frame, 0);
    if (!portable_track_object_render(&art, &frame, 32, 32, 0x100, 0x100) ||
        pixels[1u * 64u + 16u] != UINT32_C(0xff0000))
        return 1;

    puts("portable track object art: PASS");
    return 0;
}

int run_track_object_decoder(void)
{
    static uint8_t rom[0x0f1054 + 0x0c];
    PortableTrackObjectDecode decoded;
    PortableTrackObjectActorInit actor;

    memset(rom, 0, sizeof(rom));
    write_le32(rom + 0x0f1054 + 2u * 4u, 0x000c0002u);
    write_le32(rom + 0x0f1054, 0xffff0000u);
    if (portable_track_object_decode(rom, sizeof(rom), 4, 2, &decoded) !=
            PORTABLE_TRACK_OBJECT_DECODER_OK ||
        decoded.object_class != 4 || decoded.raw_type != 2 ||
        decoded.table_offset != 0x0f105c ||
        decoded.source_code != 0x000c0002u)
        return 1;
    if (portable_track_object_actor_init(decoded.source_code, &actor) !=
            PORTABLE_TRACK_OBJECT_DECODER_OK ||
        actor.kind != PORTABLE_TRACK_OBJECT_ACTOR_INIT_GENERIC_EXTENDED ||
        actor.source_actor_type != 0x1000 || actor.size_x != 8 ||
        actor.size_y != 8 || actor.size_z != 8 || actor.source_flags != 1 ||
        actor.draw_scale != 0x100 ||
        actor.update_kind != PORTABLE_TRACK_OBJECT_UPDATE_8059E00 ||
        actor.collision_kind != PORTABLE_TRACK_OBJECT_COLLISION_8059E44 ||
        actor.active != 0 || actor.timer != 0)
        return 1;
    if (!portable_track_object_uses_runtime_map_alias(0x000c0005u) ||
        !portable_track_object_uses_runtime_map_alias(0x00860000u) ||
        portable_track_object_uses_runtime_map_alias(0x00800000u) ||
        portable_track_object_uses_runtime_map_alias(0xfff00000u))
        return 1;
    if (portable_track_object_decode(rom, sizeof(rom), 4, 0, &decoded) !=
        PORTABLE_TRACK_OBJECT_DECODER_UNSUPPORTED_OBJECT)
        return 1;
    if (portable_track_object_decode(
            rom, sizeof(rom), 3, 2, &decoded) !=
            PORTABLE_TRACK_OBJECT_DECODER_UNSUPPORTED_CLASS ||
        portable_track_object_decode(
            rom, 0x0f105c, 4, 2, &decoded) !=
            PORTABLE_TRACK_OBJECT_DECODER_OUT_OF_BOUNDS ||
        portable_track_object_decode(
            NULL, sizeof(rom), 4, 2, &decoded) !=
            PORTABLE_TRACK_OBJECT_DECODER_INVALID_ARGUMENT ||
        portable_track_object_actor_init(0x000c0003u, &actor) !=
            PORTABLE_TRACK_OBJECT_DECODER_UNSUPPORTED_OBJECT ||
        portable_track_object_actor_init(0x000c0002u, NULL) !=
            PORTABLE_TRACK_OBJECT_DECODER_INVALID_ARGUMENT)
        return 1;

    puts("portable track object decoder: PASS");
    return 0;
}

int run_track_raster(void)
{
    uint8_t data[0x300] = {0};
    uint8_t cells[64 * 64];
    PortableTrackAsset asset = {
        .data = data,
        .size = sizeof(data),
    };
    PortableTrackRaster raster;
    uint8_t value;

    data[4] = 1;
    write_le32(data + 0xCC, 0x100);
    data[0x100] = 5;
    data[0x101] = 5;
    data[0x102] = 0;

    /* Rectangle: first command value wraps to 0x80. */
    data[0x105] = 0;
    data[0x106] = 1;
    data[0x108] = 1;
    /* Forward triangle, type 1, with two rows. */
    data[0x111] = 1;
    data[0x112] = 4;
    data[0x114] = 4;
    data[0x116] = 1;
    /* Backward-column triangle, type 2. */
    data[0x11d] = 2;
    data[0x11e] = 8;
    data[0x120] = 8;
    data[0x122] = 2;
    /* Backward-row and backward-column triangle, type 3. */
    data[0x129] = 3;
    data[0x12a] = 12;
    data[0x12c] = 12;
    data[0x12e] = 2;
    /* Backward-row and forward-column triangle, last value wraps to 0x84. */
    data[0x135] = 4;
    data[0x136] = 16;
    data[0x138] = 16;
    data[0x13a] = 2;

    if (portable_track_raster_build(&asset, cells, sizeof(cells), &raster) !=
            PORTABLE_TRACK_RASTER_OK ||
        raster.width != 64 || raster.height != 64 ||
        raster.command_count != 5 || raster.entry_offset != 5)
        return 1;
    if (cells[1 * 64 + 1] != 0x80 ||
        cells[4 * 64 + 4] != 1 || cells[4 * 64 + 5] != 1 ||
        cells[5 * 64 + 4] != 1 ||
        cells[8 * 64 + 8] != 2 || cells[8 * 64 + 7] != 2 ||
        cells[8 * 64 + 6] != 2 || cells[9 * 64 + 8] != 2 ||
        cells[12 * 64 + 12] != 3 || cells[12 * 64 + 11] != 3 ||
        cells[11 * 64 + 12] != 3 ||
        cells[16 * 64 + 16] != 0x84 || cells[16 * 64 + 17] != 0x84 ||
        cells[15 * 64 + 16] != 0x84 || cells[14 * 64 + 16] != 0x84)
        return 1;
    if (portable_track_raster_query16(&raster, 16 * 16, 16 * 16,
                                      &value) != PORTABLE_TRACK_RASTER_OK ||
        value != 0x84 ||
        portable_track_raster_query16(&raster, -1, -1, &value) !=
            PORTABLE_TRACK_RASTER_OK ||
        value != 0x7f ||
        portable_track_raster_query16(&raster, -16, 0, &value) !=
            PORTABLE_TRACK_RASTER_OUT_OF_BOUNDS)
        return 1;
    if (portable_track_raster_build(&asset, cells, sizeof(cells) - 1,
                                    &raster) !=
        PORTABLE_TRACK_RASTER_DESTINATION_TOO_SMALL)
        return 1;

    data[0x105] = 0;
    data[0x106] = 63;
    data[0x108] = 63;
    data[0x10a] = 3;
    data[0x10c] = 3;
    data[0x100] = 1;
    if (portable_track_raster_build(&asset, cells, sizeof(cells), &raster) !=
            PORTABLE_TRACK_RASTER_OK ||
        cells[63 * 64 + 63] != 0x80)
        return 1;

    puts("portable track raster: PASS");
    return 0;
}

int run_terrain_asset(void)
{
    static uint8_t data[0x8000];
    static uint8_t cells[0x10000];
    PortableTrackAsset asset = {
        .data = data,
        .size = sizeof(data),
    };
    PortableTerrainAssetView terrain_asset;
    uint8_t value;
    size_t index;
    size_t output;
    size_t source;

    memset(data, 0, sizeof(data));
    memset(cells, 0, sizeof(cells));
    data[3] = 2;
    data[4] = 1;
    data[5] = 1;
    write_le32(data + 0xd4, 0x100);
    for (index = 0; index < 16; ++index)
        data[0x100 + index * 2] = 0x20;
    data[0x120] = 0x10;
    data[0x121] = 1;
    data[0x122] = 0;
    data[0x123] = 0;
    data[0x124] = 0;
    data[0x125] = 0xa5;
    if (portable_terrain_asset_build(&asset, cells, sizeof(cells),
                                     &terrain_asset) !=
            PORTABLE_TERRAIN_ASSET_OK ||
        terrain_asset.view.width != 128 ||
        terrain_asset.view.height != 128 ||
        terrain_asset.view.row_stride != 128 ||
        terrain_asset.decoded_buffer_size != 0x10000 ||
        terrain_asset.block_count != 16 || cells[0] != 0xa5 ||
        cells[0x1000] != 0xa5 ||
        portable_terrain_query8(&terrain_asset.view, 0, 0, &value) !=
            PORTABLE_TERRAIN_QUERY_OK ||
        value != 0xa5)
        return 1;

    memset(data, 0, sizeof(data));
    memset(cells, 0, sizeof(cells));
    data[4] = 1;
    data[5] = 1;
    write_le32(data + 0xd4, 0x400);
    data[0x400] = 0x10;
    data[0x401] = 0;
    data[0x402] = 0x40;
    data[0x403] = 0;
    source = 0x404;
    output = 0;
    while (output < 0x4000)
    {
        data[source++] = 0;
        for (index = 0; index < 8 && output < 0x4000; ++index)
        {
            data[source++] = output == 0 ? 0x11 :
                             (output == 1 ? 0x22 : 0);
            ++output;
        }
    }
    if (portable_terrain_asset_build(&asset, cells, sizeof(cells),
                                     &terrain_asset) !=
            PORTABLE_TERRAIN_ASSET_OK ||
        terrain_asset.compressed != 0 || terrain_asset.block_count != 1 ||
        terrain_asset.decoded_buffer_size != 0x4000 || cells[0] != 0x11 ||
        cells[1] != 0x22)
        return 1;
    if (portable_terrain_asset_build(&asset, cells, 1, &terrain_asset) !=
            PORTABLE_TERRAIN_ASSET_DESTINATION_TOO_SMALL ||
        portable_terrain_asset_build(NULL, cells, sizeof(cells),
                                     &terrain_asset) !=
            PORTABLE_TERRAIN_ASSET_INVALID_ARGUMENT)
        return 1;

    puts("portable terrain asset: PASS");
    return 0;
}

static int test_packed_sky_motion(void)
{
    uint8_t packed[104]={0};
    PortableSkyMotionRules storage;
    const PortableSkyMotionRules *rules=NULL;
    PortableAssetSectionView section={.kind=PORTABLE_ASSET_SECTION_SKY_GRAPHICS,
        .content_id=1u,.version=1u,.bytes=packed,.size=64u};
    write_le32(packed,UINT32_C(0x31594b53));
    if(portable_sky_asset_open_motion(&section,&storage,&rules)!=PORTABLE_SKY_ASSET_OK || rules!=NULL)return 1;
    section.version=2u;section.size=sizeof packed;write_le32(packed+60u,40u);
    uint8_t *motion=packed+64u;
    write_le32(motion,UINT32_C(0x31524d53));write_le32(motion+4u,1u);
    motion[8]=PORTABLE_SKY_MOTION_CLOCK_SWAY;motion[9]=5u;
    motion[11]=4u;motion[12]=12u;motion[13]=4u;motion[14]=5u;motion[15]=6u;
    write_le16(motion+16u,(uint16_t)-17);write_le16(motion+22u,23u);
    write_le16(motion+28u,(uint16_t)-1);motion[30]=2u;motion[31]=7u;
    write_le16(motion+32u,(uint16_t)-19);motion[36]=2u;motion[37]=0u;motion[38]=1u;motion[39]=3u;
    if(portable_sky_asset_open_motion(&section,&storage,&rules)!=PORTABLE_SKY_ASSET_OK ||
       rules!=&storage || rules->controller!=PORTABLE_SKY_MOTION_CLOCK_SWAY ||
       rules->layer_mask!=5u || rules->yaw_shift[2]!=6u || rules->x_bias[0]!=-17 ||
       rules->y_bias[0]!=23 || rules->split_y!=-1 || rules->split_layer!=2u ||
       rules->split_yaw_shift!=7u || rules->split_x_bias!=-19 || rules->layer_source[0]!=2u || rules->road_mode!=3u)return 1;
    static const struct {size_t offset;uint8_t value;} invalid[]={
        {0u,0u},{4u,2u},{8u,8u},{9u,8u},{10u,2u},{11u,17u},{12u,17u},
        {13u,16u},{28u,160u},{29u,1u},{30u,3u},{31u,16u},{34u,3u},{35u,1u},{36u,3u},{39u,4u}};
    for(size_t index=0u;index<sizeof invalid/sizeof invalid[0];++index){
        const uint8_t original=motion[invalid[index].offset];motion[invalid[index].offset]=invalid[index].value;
        if(portable_sky_asset_open_motion(&section,&storage,&rules)==PORTABLE_SKY_ASSET_OK)return 1;
        motion[invalid[index].offset]=original;
    }
    --section.size;
    if(portable_sky_asset_open_motion(&section,&storage,&rules)==PORTABLE_SKY_ASSET_OK)return 1;
    ++section.size;section.version=1u;
    if(portable_sky_asset_open_motion(&section,&storage,&rules)==PORTABLE_SKY_ASSET_OK)return 1;
    section.version=2u;write_le32(packed+4u,UINT32_MAX);
    if(portable_sky_asset_open_motion(&section,&storage,&rules)!=PORTABLE_SKY_ASSET_CAPACITY)return 1;
    return 0;
}

int run_sky_asset(void)
{
    if(test_packed_sky_motion()!=0)return 1;
    static uint8_t data[0x10000];
    static uint8_t tiles[0x2800];
    static uint16_t palette[63];
    static uint8_t front_map[0x800];
    static uint8_t mid_map[0x1000];
    static uint8_t back_map[0x800];
    static uint32_t pixels[16 * 80];
    PortableSkyAssetBuffers buffers = {
        .tiles = tiles,
        .tile_capacity = sizeof(tiles),
        .palette = palette,
        .palette_capacity = sizeof(palette) / sizeof(palette[0]),
        .front_map = front_map,
        .front_capacity = sizeof(front_map),
        .mid_map = mid_map,
        .mid_capacity = sizeof(mid_map),
        .back_map = back_map,
        .back_capacity = sizeof(back_map),
    };
    PortableSkyAssetView sky;
    PortableSoftwareFrame frame;
    size_t background = 0x40;
    size_t gfx = background + 0x20;
    size_t source = 0x80;
    size_t block;
    size_t front = 0x4000;
    size_t mid = 0x5000;
    size_t back = 0x7000;
    size_t pal = 0x8000;

    memset(data, 0, sizeof(data));
    memset(tiles, 0, sizeof(tiles));
    memset(palette, 0, sizeof(palette));
    memset(front_map, 0, sizeof(front_map));
    memset(mid_map, 0, sizeof(mid_map));
    memset(back_map, 0, sizeof(back_map));
    write_le32(data, (uint32_t)background);
    data[background] = 0x40;
    data[background + 1] = 3;
    write_le16(data + background + 2, 6);
    write_le32(data + background + 4, 0x20);
    write_le32(data + background + 8, (uint32_t)(front - background));
    write_le32(data + background + 12, (uint32_t)(mid - background));
    write_le32(data + background + 16, (uint32_t)(back - background));
    write_le32(data + background + 20, (uint32_t)(pal - background));
    for (block = 0; block < 5; ++block)
    {
        size_t used = build_literal_lz77(data + source, sizeof(data) - source,
                                         0x800, (uint8_t)(block + 1));
        if (used == 0)
            return 1;
        write_le16(data + gfx + block * 2,
                   (uint16_t)(source - gfx));
        source += used;
    }
    if (build_literal_lz77(data + front, sizeof(data) - front, 0x800, 0) == 0 ||
        build_literal_lz77(data + mid, sizeof(data) - mid, 0x1000, 0) == 0 ||
        build_literal_lz77(data + back, sizeof(data) - back, 0x800, 0) == 0)
        return 1;
    for (block = 0; block < 63; ++block)
        write_le16(data + pal + block * 2, (uint16_t)(block + 1));

    if (portable_sky_asset_build(data, sizeof(data), 0, &buffers, &sky) !=
            PORTABLE_SKY_ASSET_OK ||
        sky.tile_bytes != 0x2800 || sky.palette_count != 63 ||
        sky.front_map_bytes != 0x800 || sky.mid_map_bytes != 0x1000 ||
        sky.back_map_bytes != 0x800 || sky.front_width_tiles != 32 ||
        sky.mid_width_tiles != 64 || sky.back_width_tiles != 32 ||
        sky.map_height_tiles != 32 || tiles[0] != 1 ||
        tiles[0x800] != 2 || tiles[0x1000] != 3 || palette[62] != 63 ||
        !portable_software_frame_init(&frame, pixels, 16, 80) ||
        portable_sky_asset_build(NULL, sizeof(data), 0, &buffers, &sky) !=
            PORTABLE_SKY_ASSET_INVALID_ARGUMENT)
        return 1;

    /* Text BG size 512x256 uses a second 32x32 screenblock at x=256. */
    tiles[0] = 0u;
    tiles[32] = 0x22u;
    write_le16(mid_map + 0x800u, UINT16_C(0x6001));
    portable_software_frame_clear(&frame, 0x111111);
    if (!portable_sky_render(&sky, &frame, 256, 0) ||
        pixels[0] == 0x111111)
        return 1;
    /* A tall layer wraps at 512 pixels independently of its 256-pixel peers. */
    {
        uint8_t tall_map[0x1000] = {0};
        PortableSkyAssetView tall = sky;
        tall.front_map = tall_map;
        tall.front_map_bytes = sizeof tall_map;
        tall.front_width_tiles = 32u;
        write_le16(tall_map + 0x800u, UINT16_C(0x6001));
        portable_software_frame_clear(&frame, 0x111111);
        if (!portable_sky_render_front(&tall, &frame, 0, 256) ||
            pixels[0] == 0x111111)
            return 1;
        portable_software_frame_clear(&frame, 0x111111);
        if (!portable_sky_render_front(&tall, &frame, 0, 512) ||
            pixels[0] != 0x111111)
            return 1;
    }
    tiles[0] = 1u;
    tiles[32] = 1u;
    write_le16(mid_map + 0x800u, 0u);

    /* Source SkyDef size 2 loads all three compressed maps as 32x32. */
    data[background] = 2u;
    if (build_literal_lz77(data + mid, sizeof(data) - mid, 0x800, 0) == 0 ||
        portable_sky_asset_build(data, sizeof(data), 0, &buffers, &sky) !=
            PORTABLE_SKY_ASSET_OK ||
        sky.size != 2u || sky.front_map_bytes != 0x800u ||
        sky.mid_map_bytes != 0x800u || sky.back_map_bytes != 0x800u ||
        sky.front_width_tiles != 32u || sky.mid_width_tiles != 32u ||
        sky.back_width_tiles != 32u)
        return 1;
    data[background] = 0x40u;
    if (build_literal_lz77(data + mid, sizeof(data) - mid, 0x1000, 0) == 0 ||
        portable_sky_asset_build(data, sizeof(data), 0, &buffers, &sky) !=
            PORTABLE_SKY_ASSET_OK)
        return 1;

    /* Exercise palette banks, both flips, transparent color zero, and layer order. */
    tiles[0] = 0x12;
    tiles[1] = 0x34;
    tiles[2] = 0x56;
    tiles[3] = 0x78;
    tiles[28] = 0x98;
    tiles[29] = 0x06;
    tiles[30] = 0x54;
    tiles[31] = 0x32;
    palette[2] = 0x03e0;
    palette[3] = 0x7c00;
    palette[6] = 0x001f;
    write_le16(back_map, 0x6000);
    write_le16(front_map, 0x6000 | 0x0400 | 0x0800);
    portable_software_frame_clear(&frame, 0x111111);
    if (!portable_sky_render(&sky, &frame, 0, 0) ||
        pixels[0] != 0x0000ff || pixels[1] != 0x00ff00 ||
        pixels[4] != 0xff0000)
        return 1;

    /* Lightning exposes the backdrop through transparent middle pixels;
     * distant pixels do not leak through, and the foreground stays opaque. */
    tiles[32]=0x20u;
    write_le16(mid_map,UINT16_C(0x6001));
    for(unsigned white=0u;white<2u;++white){
        const uint32_t backdrop=white?0xffffffu:0u;
        portable_software_frame_clear(&frame,backdrop);
        if(!portable_sky_render_middle(&sky,&frame,0,0) ||
           pixels[0]!=backdrop || pixels[1]!=0x00ff00 ||
           !portable_sky_render_front(&sky,&frame,0,0) || pixels[0]!=0x0000ff)
            return 1;
    }
    write_le16(mid_map,0u);

    /* The original result camera keeps moving karts against a fixed sky.
     * Source BG1 uses signed yaw / 64 minus the viewport margin, so these
     * two camera headings must sample different halves of the same tile. */
    for (size_t row = 0u; row < 8u; ++row)
        for (size_t column = 0u; column < 4u; ++column)
            tiles[row * 4u + column] = column < 2u ? 0x11u : 0x22u;
    for (size_t index = 0u; index < sizeof front_map / 2u; ++index)
        write_le16(front_map + index * 2u, UINT16_C(0x6000));
    palette[1] = UINT16_C(0x7c00);
    palette[2] = UINT16_C(0x03e0);
    portable_software_frame_clear(&frame, 0x111111);
    if (!portable_sky_render_race_back(&sky, &frame, 0) ||
        !portable_sky_render_race_front(&sky, &frame, 0) ||
        pixels[0] != 0x0000ff)
        return 1;
    portable_software_frame_clear(&frame, 0x111111);
    if (!portable_sky_render_race_back(&sky, &frame,
                                      (-INT16_C(2338))) ||
        !portable_sky_render_race_front(&sky, &frame,
                                       (-INT16_C(2338))) ||
        pixels[0] != 0x00ff00)
        return 1;
    palette[1] = UINT16_C(0x000c);
    portable_software_frame_clear(&frame, 0x111111);
    if (!portable_sky_render_race_front(&sky, &frame, 0) ||
        pixels[0] != 0x630000)
        return 1;

    /* BLDCNT=0x2c42/BLDALPHA=0x1008 also blends the dark BG1 horizon
     * over road pixels. The source's (70,202,20) result is the road's
     * (66,198,16) plus half of foreground (8,8,8). */
    palette[1] = UINT16_C(0x0421);
    portable_software_frame_clear(&frame, 0x42c610);
    if (!portable_sky_render_race_front(&sky, &frame, 0) ||
        pixels[0] != 0x080808 || pixels[74u * frame.width] != 0x46ca14)
        return 1;
    portable_software_frame_clear(&frame, 0x42c610);
    if (!portable_sky_render_race_front_scrolled_offset(
            &sky, &frame, -40, 70, 1u) ||
        pixels[74u * frame.width] != 0x080808 ||
        pixels[75u * frame.width] != 0x46ca14 ||
        portable_sky_render_race_front_scrolled_offset(
            &sky, &frame, -40, 70, 25u))
        return 1;

    puts("portable sky asset: PASS");
    return 0;
}

int run_character_asset(void)
{
    static uint8_t tile_data[0x38800];
    static uint8_t direction_tiles[
        PORTABLE_CHARACTER_DIRECTION_FRAME_COUNT *
        PORTABLE_CHARACTER_FRAME_BYTES];
    static uint8_t view_frame_slots[PORTABLE_CHARACTER_VIEW_SLOT_COUNT];
    static uint8_t palette_data[0x420];
    static uint16_t palette[PORTABLE_CHARACTER_PALETTE_COUNT];
    static uint32_t pixels[80 * 80];
    static uint8_t packed[32u+PORTABLE_CHARACTER_VIEW_SLOT_COUNT+
        PORTABLE_CHARACTER_DIRECTION_FRAME_COUNT*
        PORTABLE_CHARACTER_FRAME_BYTES+
        PORTABLE_CHARACTER_PALETTE_COUNT*2u];
    static PortableCharacterPackedStorage packed_storage;
    PortableCharacterAssetBuffers buffers = {
        .palette = palette,
        .palette_capacity = PORTABLE_CHARACTER_PALETTE_COUNT,
    };
    PortableCharacterAssetView character;
    PortableSoftwareFrame frame;
    PortableCharacterViewInput view_input = {0};
    PortableAssetSectionView packed_section={
        .kind=PORTABLE_ASSET_SECTION_CHARACTER_GRAPHICS,
        .content_id=1u,.version=1u,.bytes=packed,.size=sizeof packed};

    PortableCharacterViewRules view_rules={0};
    view_rules.speed_limit[2]=2u;
    view_rules.response_limit[2]=2u;
    view_input.selected=1u;
    if (portable_character_view_pose(NULL,&view_rules).angle != 0u ||
        portable_character_view_pose(&view_input,&view_rules).angle != 127u)
        return 1;
    view_input.steering_response_q8 = INT16_C(0x0200);
    view_input.speed_q16 = INT32_C(0x00400000);
    if (portable_character_view_pose(&view_input,&view_rules).angle != 126u)
        return 1;
    view_input.steering_response_q8 = -INT16_C(0x0200);
    if (portable_character_view_pose(&view_input,&view_rules).angle != 1u)
        return 1;

    memset(tile_data, 0, sizeof(tile_data));
    memset(palette_data, 0, sizeof(palette_data));
    memset(palette, 0, sizeof(palette));
    tile_data[0x38000] = 0x12;
    tile_data[0x38000 + 7u * 32u + 3u] = 0x30;
    tile_data[0x38000 + 56u * 32u + 28u] = 0x44;
    write_le16(palette_data + 0x400 + 2, 0x001f);
    if (portable_character_asset_build(
            tile_data, sizeof(tile_data), palette_data, sizeof(palette_data),
            2, 0x18, &buffers, &character) !=
            PORTABLE_CHARACTER_ASSET_OK ||
        character.tile_offset != 0x38000 || character.palette_offset != 0x400 ||
        character.tile_bytes != 0x800 || character.width != 64 ||
        character.height != 64 || palette[1] != 0x001f ||
        !portable_software_frame_init(&frame, pixels, 80, 80))
        return 1;

    portable_software_frame_clear(&frame, 0x112233);
    palette[2] = 0x03e0;
    palette[3] = 0x7c00;
    palette[4] = 0x7fff;
    if (!portable_character_render(&character, &frame, 8, 8, 0, 0) ||
        pixels[8u * 80u + 8u] != 0x00ff00 || pixels[0] != 0x112233)
        return 1;
    portable_software_frame_clear(&frame, 0x112233);
    if (!portable_character_render(&character, &frame, 8, 8, 1, 0) ||
        pixels[8u * 80u + 8u] != 0x0000ff)
        return 1;
    portable_software_frame_clear(&frame, 0x112233);
    if (!portable_character_render(&character, &frame, 8, 8, 0, 1) ||
        pixels[8u * 80u + 8u] != 0xffffff ||
        portable_character_asset_build(
            NULL, sizeof(tile_data), palette_data, sizeof(palette_data), 2,
            0x18, &buffers, &character) !=
            PORTABLE_CHARACTER_ASSET_INVALID_ARGUMENT)
        return 1;

    memset(direction_tiles, 0, sizeof direction_tiles);
    for (size_t index = 0; index < PORTABLE_CHARACTER_VIEW_SLOT_COUNT;
         ++index)
        view_frame_slots[index] = (uint8_t)(index / 3u);
    direction_tiles[5u * PORTABLE_CHARACTER_FRAME_BYTES] = 0x02;
    character.direction_tiles = direction_tiles;
    character.direction_tile_bytes = sizeof direction_tiles;
    character.direction_frame_count =
        PORTABLE_CHARACTER_DIRECTION_FRAME_COUNT;
    character.view_frame_slots = view_frame_slots;
    character.view_frame_slot_count = PORTABLE_CHARACTER_VIEW_SLOT_COUNT;
    portable_software_frame_clear(&frame, 0x112233);
    if (!portable_character_render_view(&character, &frame, 8, 8, 15u, 0) ||
        pixels[8u * 80u + 8u] != 0x00ff00 ||
        portable_character_render_view(&character, &frame, 8, 8, 128u, 0))
        return 1;
    portable_software_frame_clear(&frame, 0x112233);
    if (!portable_character_render_view_affine(
            &character, &frame, 8, 8, 15u, 0, UINT16_C(0x0180)) ||
        pixels[18u * 80u + 18u] != 0x00ff00 ||
        pixels[8u * 80u + 8u] != 0x112233)
        return 1;

    /* Independent vertical scaling compresses the kart while retaining the
     * bottom of its native canvas; horizontal sampling stays unchanged. */
    portable_software_frame_clear(&frame, 0x112233);
    if (!portable_character_render_view_scaled(
            &character, &frame, 8, 8, 15u, 0, 0u, 0x300u, 1) ||
        pixels[50u * 80u + 8u] != 0x00ff00 ||
        pixels[8u * 80u + 8u] != 0x112233 ||
        pixels[49u * 80u + 8u] != 0x112233)
        return 1;

    /* Source driver-cell pivot differs from the generic bottom anchor:
     * inverseY=768 moves the centre down10, and the22px-high canvas starts
     *21px below its unscaled top. Unscaled rendering retains its baseline. */
    portable_software_frame_clear(&frame,0x112233u);
    if(!portable_character_render_driver_view(&character,&frame,8,8,15u,256u,768u) ||
       pixels[39u*80u+8u]!=0x00ff00u || pixels[38u*80u+8u]!=0x112233u)
        return 1;
    portable_software_frame_clear(&frame,0x112233u);
    if(!portable_character_render_driver_view(&character,&frame,8,8,15u,256u,256u) ||
       pixels[8u*80u+8u]!=0x00ff00u)return 1;

    /* Source driver cells select their own LOD art, retain the negative-X
     * affine pivot quirk, and use direct mirroring on an unscaled cell. */
    {
        uint8_t near_tiles[2048],mid_tiles[512],far_tiles[128],driver_slots[64]={0};
        memset(near_tiles,0x11,sizeof near_tiles);memset(mid_tiles,0x22,sizeof mid_tiles);
        memset(far_tiles,0x33,sizeof far_tiles);
        const uint16_t driver_palette[16]={0,0x001f,0x03e0,0x7c00};
        PortableCharacterAssetView driver=character;
        driver.direction_tiles=near_tiles;driver.direction_tile_bytes=sizeof near_tiles;
        driver.direction_frame_count=1u;driver.view_frame_slots=driver_slots;
        driver.lod32_tiles=mid_tiles;driver.lod16_tiles=far_tiles;
        driver.palette=driver_palette;driver.palette_count=16u;
        const PortableDriverProjectionRules rules={.rows={
            {511u,64u,-32,-46,1u},{1023u,32u,-16,-23,1u},{4096u,16u,-8,-11,1u}}};
        PortableCameraProjectionResult body={40,60,256};
        portable_software_frame_clear(&frame,0x112233u);
        if(!portable_character_render_driver_projected(&driver,&frame,&rules,&body,0u,256u,256u,256u,0) ||
           pixels[14u*80u+8u]!=0xff0000u || pixels[13u*80u+8u]!=0x112233u)return 1;
        body.distance=512;
        portable_software_frame_clear(&frame,0x112233u);
        if(!portable_character_render_driver_projected(&driver,&frame,&rules,&body,0u,256u,256u,256u,0) ||
           pixels[37u*80u+24u]!=0x00ff00u || pixels[36u*80u+24u]!=0x112233u)return 1;
        body.distance=1024;
        portable_software_frame_clear(&frame,0x112233u);
        if(!portable_character_render_driver_projected(&driver,&frame,&rules,&body,0u,256u,256u,256u,0) ||
           pixels[49u*80u+32u]!=0x0000ffu)return 1;
        body.distance=384;
        portable_software_frame_clear(&frame,0x112233u);
        if(!portable_character_render_driver_projected(&driver,&frame,&rules,&body,0u,256u,256u,256u,0) ||
           pixels[30u*80u+18u]!=0xff0000u || pixels[30u*80u+17u]!=0x112233u)return 1;
        body.distance=256;near_tiles[0]=0x12u;
        portable_software_frame_clear(&frame,0x112233u);
        if(!portable_character_render_driver_projected(&driver,&frame,&rules,&body,80u,256u,256u,256u,0) ||
           pixels[14u*80u+71u]!=0x00ff00u || pixels[14u*80u+8u]!=0xff0000u)return 1;
        /* Custom drivers without separate LOD banks use bounded sampling of
         * their own full-size art, rather than borrowing a stock driver's. */
        driver.lod32_tiles=NULL;driver.lod16_tiles=NULL;body.distance=1024;
        portable_software_frame_clear(&frame,0x112233u);
        if(!portable_character_render_driver_projected(&driver,&frame,&rules,&body,0u,256u,256u,256u,0) ||
           pixels[50u*80u+33u]!=0xff0000u)return 1;
    }

    {
        /* Resolved contact art and mirror must not be reconstructed from the
         * angle. Both exact and affine paths keep the immutable pose. */
        uint8_t pose_tiles[4096],pose_mid[1024],slots[64]={0};
        uint32_t expected[80u*80u];
        memset(pose_tiles,0x11,2048);memset(pose_tiles+2048,0x33,2048);
        memset(pose_mid,0x22,sizeof pose_mid);
        pose_tiles[2048]=0x32u;
        const uint16_t colors[16]={0,0x001f,0x03e0,0x7c00};
        PortableCharacterAssetView driver=character;
        driver.direction_tiles=pose_tiles;driver.direction_tile_bytes=sizeof pose_tiles;
        driver.direction_frame_count=2;driver.view_frame_slots=slots;
        driver.view_frame_slot_count=64;driver.lod32_tiles=pose_mid;
        driver.palette=colors;driver.palette_count=16;
        const PortableDriverProjectionRules rules={.rows={
            {511u,64u,-32,-46,1u},{1023u,32u,-16,-23,1u},{4096u,16u,-8,-11,1u}}};
        PortableCameraProjectionResult body={40,60,256};
        PortableCharacterViewPose pose={.angle=80,.frame_override=1,.mirror=0};
        const PortableCharacterViewPose before_pose=pose;
        const PortableCharacterAssetView before_driver=driver;
        portable_software_frame_clear(&frame,0x112233u);
        if(!portable_character_render_driver_pose_projected(&driver,&frame,&rules,&body,&pose,256,256,256,0) ||
           pixels[14u*80u+8u]!=0x00ff00u || pixels[14u*80u+71u]!=0x0000ffu)return 1;
        pose.mirror=1;
        portable_software_frame_clear(&frame,0x112233u);
        if(!portable_character_render_driver_pose_projected(&driver,&frame,&rules,&body,&pose,256,256,256,0) ||
           pixels[14u*80u+71u]!=0x00ff00u)return 1;
        /* Near contact art stops at the source distance boundary. */
        body.distance=511;
        portable_software_frame_clear(&frame,0x112233u);
        if(!portable_character_render_driver_pose_projected(&driver,&frame,&rules,&body,&pose,256,256,256,0))return 1;
        int blue=0;
        for(size_t i=0;i<80u*80u;++i)blue+=pixels[i]==0x0000ffu;
        if(!blue)return 1;
        body.distance=512;pose.frame_override=255;
        portable_software_frame_clear(&frame,0x112233u);
        if(!portable_character_render_driver_pose_projected(&driver,&frame,&rules,&body,&pose,256,256,256,0))return 1;
        memcpy(expected,pixels,sizeof expected);
        pose.frame_override=0;
        portable_software_frame_clear(&frame,0x112233u);
        if(!portable_character_render_driver_pose_projected(&driver,&frame,&rules,&body,&pose,256,256,256,0) ||
           memcmp(expected,pixels,sizeof expected))return 1;
        /* Explicit near frames also select the doubled affine canvas. */
        body.distance=192;pose.frame_override=1;pose.angle=0;pose.mirror=0;
        portable_software_frame_clear(&frame,0x112233u);
        if(!portable_character_render_driver_pose_projected(&driver,&frame,&rules,&body,&pose,256,256,256,1))return 1;
        blue=0;for(size_t i=0;i<80u*80u;++i)blue+=pixels[i]==0x0000ffu;
        if(!blue || memcmp(&driver,&before_driver,sizeof driver))return 1;
        pose=before_pose;
        portable_software_frame_clear(&frame,0x112233u);
        if(!portable_character_render_driver_pose(&driver,&frame,8,8,&pose,256,256) ||
           pixels[8u*80u+8u]!=0x00ff00u || memcmp(&pose,&before_pose,sizeof pose))return 1;
        body.distance=256;pose.frame_override=2;
        portable_software_frame_clear(&frame,0x112233u);
        if(!portable_character_render_driver_pose_projected(&driver,&frame,&rules,&body,&pose,256,256,256,0))return 1;
        memcpy(expected,pixels,sizeof expected);
        pose.frame_override=0;
        portable_software_frame_clear(&frame,0x112233u);
        if(!portable_character_render_driver_pose_projected(&driver,&frame,&rules,&body,&pose,256,256,256,0) ||
           memcmp(expected,pixels,sizeof expected))return 1;
    }

    memset(packed,0,sizeof packed);
    write_le32(packed,UINT32_C(0x32524143));
    write_le32(packed+4u,PORTABLE_CHARACTER_DIRECTION_FRAME_COUNT*
                              PORTABLE_CHARACTER_FRAME_BYTES);
    write_le32(packed+8u,PORTABLE_CHARACTER_PALETTE_COUNT);
    write_le32(packed+12u,PORTABLE_CHARACTER_WIDTH);
    write_le32(packed+16u,PORTABLE_CHARACTER_HEIGHT);
    write_le32(packed+20u,3u);
    write_le32(packed+24u,PORTABLE_CHARACTER_DIRECTION_FRAME_COUNT);
    write_le32(packed+28u,PORTABLE_CHARACTER_VIEW_SLOT_COUNT);
    packed[32u]=4u;
    packed[32u+PORTABLE_CHARACTER_VIEW_SLOT_COUNT]=0x21u;
    write_le16(packed+sizeof packed-PORTABLE_CHARACTER_PALETTE_COUNT*2u+2u,
               UINT16_C(0x1234));
    if(portable_character_asset_open_packed(
           &packed_section,&packed_storage,&character)!=
               PORTABLE_CHARACTER_ASSET_OK||
       character.character_id!=3u||character.direction_frame_count!=
           PORTABLE_CHARACTER_DIRECTION_FRAME_COUNT||
       character.view_frame_slots[0]!=4u||
       character.direction_tiles[0]!=0x21u||character.palette[1]!=0x1234u)
        return 1;
    packed_section.size--;
    if(portable_character_asset_open_packed(
           &packed_section,&packed_storage,&character)!=
               PORTABLE_CHARACTER_ASSET_FORMAT)
        return 1;

    /* Version3 preserves all source banks and replaces only body colors. */
    static uint8_t effects[32u+PORTABLE_CHARACTER_VIEW_SLOT_COUNT+
        PORTABLE_CHARACTER_FULL_FRAME_COUNT*PORTABLE_CHARACTER_FRAME_BYTES+512u];
    write_le32(effects,0x32524143u);
    write_le32(effects+4u,PORTABLE_CHARACTER_FULL_FRAME_COUNT*PORTABLE_CHARACTER_FRAME_BYTES);
    write_le32(effects+8u,256u);write_le32(effects+12u,64u);write_le32(effects+16u,64u);
    write_le32(effects+24u,PORTABLE_CHARACTER_FULL_FRAME_COUNT);
    write_le32(effects+28u,PORTABLE_CHARACTER_VIEW_SLOT_COUNT);
    for(unsigned i=0;i<256u;++i)write_le16(effects+sizeof effects-512u+i*2u,(uint16_t)(0x100u+i));
    packed_section.version=3u;packed_section.bytes=effects;packed_section.size=sizeof effects;
    uint16_t selected[16];
    if(portable_character_asset_open_packed(&packed_section,&packed_storage,&character)!=PORTABLE_CHARACTER_ASSET_OK ||
       character.palette_bank_count!=16u || !portable_character_asset_palette(&character,7u,selected))return 1;
    for(unsigned i=0;i<16u;++i)if(selected[i]!=(uint16_t)(0x100u+i+(i<3u?0u:7u*16u)))return 1;
    if(portable_character_asset_palette(&character,16u,selected))return 1;
    write_le32(effects+8u,16u);
    if(portable_character_asset_open_packed(&packed_section,&packed_storage,&character)!=PORTABLE_CHARACTER_ASSET_FORMAT)return 1;
    character.palette_banks=NULL;character.palette_bank_count=0u;
    if(!portable_character_asset_palette(&character,0u,selected) ||
        portable_character_asset_palette(&character,7u,selected))return 1;
    /* Variable-length v4 retains all sixteen bomb views and both detail
     * levels, borrowing immutable byte art but decoding little-endian colors. */
    static uint8_t bomb[32u+64u+16u*(0x800u+0x200u+0x80u)+512u];
    write_le32(bomb,0x33524143u);write_le32(bomb+4u,16u*0x800u);
    write_le32(bomb+8u,256u);write_le32(bomb+12u,64u);write_le32(bomb+16u,64u);
    write_le32(bomb+20u,8u);write_le32(bomb+24u,16u);write_le32(bomb+28u,64u);
    bomb[32u]=14u;bomb[96u]=0x21u;
    write_le16(bomb+sizeof bomb-512u+2u,0x2345u);
    packed_section.version=4u;packed_section.bytes=bomb;packed_section.size=sizeof bomb;
    uint16_t bomb_palette[256];
    if(portable_character_asset_open_borrowed(&packed_section,bomb_palette,256u,&character)!=0 ||
       character.direction_frame_count!=16u || character.direction_tiles!=bomb+96u ||
       character.lod32_tiles!=bomb+96u+0x8000u ||
       character.lod16_tiles!=bomb+96u+0xa000u || character.palette[1]!=0x2345u ||
       portable_character_asset_open_borrowed(&packed_section,bomb_palette,16u,&character)!=
           PORTABLE_CHARACTER_ASSET_CAPACITY) return 1;
    if(portable_character_asset_open_packed(&packed_section,&packed_storage,&character)!=0 ||
       character.direction_tiles==bomb+96u || character.direction_tiles[0]!=0x21u ||
       character.frame_slot!=14u || character.palette[1]!=0x2345u) return 1;
    bomb[32u]=16u;
    if(portable_character_asset_open_borrowed(&packed_section,bomb_palette,256u,&character)!=
           PORTABLE_CHARACTER_ASSET_FORMAT) return 1;
    bomb[32u]=14u;--packed_section.size;
    if(portable_character_asset_open_borrowed(&packed_section,bomb_palette,256u,&character)!=
           PORTABLE_CHARACTER_ASSET_FORMAT) return 1;
    ++packed_section.size;write_le32(bomb+24u,26u);
    if(portable_character_asset_open_borrowed(&packed_section,bomb_palette,256u,&character)!=
           PORTABLE_CHARACTER_ASSET_FORMAT) return 1;
    /* Legacy CAR1 is shared with presentation_pack rather than privately
     * re-parsed by a full course owner. Preserve every published field. */
    static uint8_t legacy[32u + PORTABLE_CHARACTER_FRAME_BYTES + 32u];
    write_le32(legacy, UINT32_C(0x31524143));
    write_le32(legacy + 4u, PORTABLE_CHARACTER_FRAME_BYTES);
    write_le32(legacy + 8u, 16u);
    write_le32(legacy + 12u, 64u);
    write_le32(legacy + 16u, 64u);
    write_le32(legacy + 20u, 200u);
    write_le32(legacy + 24u, 19u);
    legacy[32u] = 0x21u;
    write_le16(legacy + sizeof legacy - 30u, 0x1234u);
    packed_section = (PortableAssetSectionView){.kind = PORTABLE_ASSET_SECTION_CHARACTER_GRAPHICS,
                                                .content_id = 500u,
                                                .version = 1u,
                                                .bytes = legacy,
                                                .size = sizeof legacy};
    if (portable_character_asset_open_packed(&packed_section, &packed_storage, &character) !=
            PORTABLE_CHARACTER_ASSET_OK ||
        character.character_id != 200u || character.frame_slot != 19u ||
        character.tiles != packed_storage.tiles || character.tiles[0] != 0x21u ||
        character.palette[1] != 0x1234u || character.palette_bank_count != 1u ||
        character.direction_frame_count != 1u ||
        character.direction_tiles != packed_storage.tiles || character.view_frame_slots != NULL ||
        character.lod32_tiles != NULL || character.lod16_tiles != NULL)
        return 1;
    --packed_section.size;
    if (portable_character_asset_open_packed(&packed_section, &packed_storage, &character) !=
        PORTABLE_CHARACTER_ASSET_FORMAT)
        return 1;
    ++packed_section.size;
    write_le32(legacy + 12u, 32u);
    if (portable_character_asset_open_packed(&packed_section, &packed_storage, &character) !=
        PORTABLE_CHARACTER_ASSET_FORMAT)
        return 1;
    puts("portable character asset: PASS");
    return 0;
}

int run_track_tileset(void)
{
    static uint8_t data[0x5000];
    static uint8_t tiles[PORTABLE_TRACK_TILESET_TILE_BYTES];
    static uint16_t palette[PORTABLE_TRACK_TILESET_PALETTE_COUNT];
    const size_t table_offset = 0x100;
    const size_t palette_offset = 0x4a00;
    const size_t stream_start = 0x120;
    PortableTrackAsset asset = {
        .data = data,
        .size = sizeof(data),
    };
    PortableTrackTilesetBuffers buffers = {
        .tiles = tiles,
        .tile_capacity = sizeof(tiles),
        .palette = palette,
        .palette_capacity = sizeof(palette) / sizeof(palette[0]),
    };
    PortableTrackTilesetView view;
    PortableTrackTilesetStatus status;
    size_t block;
    size_t source = stream_start;
    const size_t common_source = 0x3000;

    memset(data, 0, sizeof(data));
    memset(tiles, 0, sizeof(tiles));
    memset(palette, 0, sizeof(palette));
    data[3] = 3;
    write_le32(data + 0x80, (uint32_t)table_offset);
    write_le32(data + 0x84, (uint32_t)palette_offset);
    for (block = 0; block < PORTABLE_TRACK_TILESET_BLOCK_COUNT; ++block)
    {
        size_t used;

        write_le16(data + table_offset + block * 2u,
                   (uint16_t)(source - table_offset));
        used = build_literal_lz77(
            data + source, sizeof(data) - source,
            PORTABLE_TRACK_TILESET_BLOCK_BYTES,
            (uint8_t)(0x11u * (block + 1u)));
        if (used == 0)
            return 1;
        source += used;
    }
    for (block = 0; block < PORTABLE_TRACK_TILESET_PALETTE_COUNT; ++block)
        write_le16(data + palette_offset + block * 2u,
                   (uint16_t)(0x4000u + block));

    status = portable_track_tileset_asset_build(&asset, &buffers, &view);
    if (status != PORTABLE_TRACK_TILESET_OK ||
        view.tiles != tiles || view.tile_bytes != sizeof(tiles) ||
        view.palette != palette || view.palette_count != 48 ||
        view.table_offset != table_offset ||
        view.palette_offset != palette_offset || view.flags != 3 ||
        view.block_count != 4 || tiles[0] != 0x11 ||
        tiles[0x1000] != 0x22 || tiles[0x2000] != 0x33 ||
        tiles[0x3000] != 0x44 || tiles[0x3fff] != 0x44 ||
        palette[0] != 0x4000 || palette[47] != 0x402f)
        return 1;

    data[3] = 0;
    if (portable_track_tileset_asset_build(&asset, &buffers, &view) !=
        PORTABLE_TRACK_TILESET_UNSUPPORTED)
        return 1;
    data[3] = 3;
    buffers.tile_capacity = sizeof(tiles) - 1u;
    if (portable_track_tileset_asset_build(&asset, &buffers, &view) !=
        PORTABLE_TRACK_TILESET_CAPACITY)
        return 1;
    buffers.tile_capacity = sizeof(tiles);
    write_le32(data + 0x80, (uint32_t)(sizeof(data) - 4u));
    if (portable_track_tileset_asset_build(&asset, &buffers, &view) !=
        PORTABLE_TRACK_TILESET_OUT_OF_BOUNDS)
        return 1;
    write_le32(data + 0x80, (uint32_t)table_offset);
    data[stream_start] = 0x10;
    data[stream_start + 1] = 0;
    data[stream_start + 2] = 0x10;
    data[stream_start + 3] = 0;
    {
        PortableTrackAsset short_asset = asset;

        short_asset.size = stream_start + 5u;
        if (portable_track_tileset_asset_build(&short_asset, &buffers,
                                               &view) !=
            PORTABLE_TRACK_TILESET_OUT_OF_BOUNDS)
            return 1;
    }
    data[stream_start] = 0;
    if (portable_track_tileset_asset_build(&asset, &buffers, &view) !=
        PORTABLE_TRACK_TILESET_FORMAT)
        return 1;

    memset(tiles, 0, sizeof(tiles));
    if (build_literal_lz77(
            data + common_source, sizeof(data) - common_source,
            PORTABLE_TRACK_TILESET_COMMON_SOURCE_BYTES, 0x5a) == 0 ||
        portable_track_tileset_asset_load_common(
            data, sizeof(data), (uint32_t)common_source, tiles,
            sizeof(tiles)) != PORTABLE_TRACK_TILESET_OK ||
        tiles[PORTABLE_TRACK_TILESET_COMMON_SOURCE_TILE * 64u] != 0x5a ||
        tiles[(PORTABLE_TRACK_TILESET_COMMON_SOURCE_TILE + 0x1fu) * 64u] !=
            0x5a ||
        portable_track_tileset_asset_load_common(
            data, sizeof(data), (uint32_t)common_source, tiles,
            sizeof(tiles) - 1u) != PORTABLE_TRACK_TILESET_CAPACITY)
        return 1;

    puts("portable track tileset: PASS");
    return 0;
}

int run_road_map(void)
{
    static uint8_t data[0x10000];
    static uint8_t map[PORTABLE_ROAD_MAP_BYTES];
    PortableRoadMapBuffers buffers = {
        .map = map,
        .map_capacity = sizeof(map),
    };
    PortableRoadMapView view;
    PortableRoadMapStatus status;
    size_t first_used;
    size_t second_source;
    size_t second_used;

    memset(data, 0, sizeof(data));
    memset(map, 0, sizeof(map));
    first_used = build_literal_lz77(data + 0x40, sizeof(data) - 0x40,
                                    PORTABLE_ROAD_MAP_BLOCK_BYTES, 0x3c);
    second_source = 0x40 + first_used;
    second_used = build_literal_lz77(
        data + second_source, sizeof(data) - second_source,
        PORTABLE_ROAD_MAP_BLOCK_BYTES, 0x3d);
    if (first_used == 0 || second_used == 0)
        return 1;
    status = portable_road_map_asset_build(data, sizeof(data), 0x40,
                                           (uint32_t)second_source, &buffers,
                                           &view);
    if (status != PORTABLE_ROAD_MAP_OK || view.map != map ||
        view.map_bytes != PORTABLE_ROAD_MAP_BYTES ||
        view.width_tiles != PORTABLE_ROAD_MAP_WIDTH_TILES ||
        view.height_tiles != PORTABLE_ROAD_MAP_HEIGHT_TILES ||
        view.source_offset != 0x40 ||
        view.source_offset_second != second_source || map[0] != 0x3c ||
        map[PORTABLE_ROAD_MAP_BLOCK_BYTES - 1u] != 0x3c ||
        map[PORTABLE_ROAD_MAP_BLOCK_BYTES] != 0x3d ||
        map[sizeof(map) - 1] != 0x3d)
        return 1;
    buffers.map_capacity = sizeof(map) - 1u;
    if (portable_road_map_asset_build(data, sizeof(data), 0x40,
                                      (uint32_t)second_source, &buffers,
                                      &view) != PORTABLE_ROAD_MAP_CAPACITY)
        return 1;
    buffers.map_capacity = sizeof(map);
    if (portable_road_map_asset_build(data, second_source + 4u, 0x40,
                                      (uint32_t)second_source, &buffers,
                                      &view) != PORTABLE_ROAD_MAP_OUT_OF_BOUNDS)
        return 1;
    data[0x40] = 0;
    if (portable_road_map_asset_build(data, sizeof(data), 0x40,
                                      (uint32_t)second_source, &buffers,
                                      &view) != PORTABLE_ROAD_MAP_FORMAT)
        return 1;

    {
        static uint8_t course_data[0x20000];
        static uint8_t course_map[PORTABLE_ROAD_COURSE_MAP_STORAGE_BYTES];
        PortableTrackAsset course_asset = {
            .data = course_data,
            .size = sizeof(course_data),
            .source_offset = 0x4000,
        };
        PortableRoadMapBuffers course_buffers = {
            .map = course_map,
            .map_capacity = sizeof(course_map),
        };
        size_t course_source = 0x200;
        size_t first_course_source;
        size_t second_course_source;

        memset(course_data, 0, sizeof(course_data));
        memset(course_map, 0, sizeof(course_map));
        course_data[3] = 2;
        course_data[4] = course_data[5] = 2;
        write_le32(course_data + 0xd4, 0x100);
        first_course_source = course_source;
        course_source += build_literal_lz77(
            course_data + course_source, sizeof(course_data) - course_source,
            PORTABLE_ROAD_COURSE_MAP_DECODE_BYTES, 1);
        second_course_source = course_source;
        course_source += build_literal_lz77(
            course_data + course_source, sizeof(course_data) - course_source,
            PORTABLE_ROAD_COURSE_MAP_DECODE_BYTES, 2);
        if (course_source == first_course_source ||
            course_source == second_course_source)
            return 1;
        for (size_t slot = 0; slot < PORTABLE_ROAD_COURSE_MAP_SLOTS;
             ++slot)
        {
            size_t source = slot == 1u ? second_course_source :
                                        first_course_source;
            write_le16(course_data + 0x100u + slot * 2u,
                       (uint16_t)(source - 0x100u));
        }
        if (portable_road_map_asset_build_course(
                &course_asset, &course_buffers, &view) !=
                PORTABLE_ROAD_MAP_OK ||
            view.map != course_map || view.map_bytes != 0x10000 ||
            view.width_tiles != 256 || view.height_tiles != 256 ||
            view.source_offset != 0x4100 || view.source_offset_second != 0x4120 ||
            course_map[0] != 1 || course_map[PORTABLE_ROAD_MAP_WIDTH_TILES] != 1 ||
            course_map[8u * PORTABLE_ROAD_MAP_WIDTH_TILES] != 1 ||
            course_map[0x800u] != 1 || course_map[0x1000u] != 2 ||
            course_map[0x1fffu] != 2 || course_map[0x2000u] != 1)
            return 1;
    }

    {
        static uint8_t repack_map[PORTABLE_ROAD_COURSE_MAP_STORAGE_BYTES];
        static int32_t sine[PORTABLE_SOURCE_SINE_COUNT];
        static uint8_t window[PORTABLE_ROAD_MAP_BYTES];
        static uint8_t saved[PORTABLE_ROAD_COURSE_MAP_STORAGE_BYTES];
        static uint8_t first_window[PORTABLE_ROAD_MAP_BYTES];
        PortableRoadMapView world={repack_map,sizeof repack_map,256,256,0,0};
        PortableRoadMapBuffers window_buffers={window,sizeof window};
        PortableRoadMapView repacked;

        /* The source rolling-map cursor is half-resolution.  Fill each
         * source row with its row number so a missing signed divide-by-two
         * moves the copied row window to a different destination. */
        memset(repack_map, 0, sizeof(repack_map));
        memset(sine, 0, sizeof(sine));
        for (size_t row = 0; row < PORTABLE_ROAD_MAP_HEIGHT_TILES; ++row)
        {
            const size_t source_row = row * PORTABLE_ROAD_MAP_WIDTH_TILES * 4u;

            for (size_t column = 0; column <
                                  PORTABLE_ROAD_MAP_WIDTH_TILES; ++column)
            {
                const size_t offset = source_row + column * 2u;

                repack_map[offset] = (uint8_t)(row + 1u);
                repack_map[offset + 1u] = 0;
                repack_map[offset + PORTABLE_ROAD_MAP_WIDTH_TILES * 2u] =
                    (uint8_t)(row + 1u);
                repack_map[offset + PORTABLE_ROAD_MAP_WIDTH_TILES * 2u + 1u] =
                    0;
            }
        }
        if (portable_road_map_asset_repack(
                &world, &window_buffers, INT32_C(80) << 19, INT32_C(80) << 19, 0,
                sine, PORTABLE_SOURCE_SINE_COUNT, &repacked) !=
                PORTABLE_ROAD_MAP_OK ||
            repacked.map != window ||
            window[16u * PORTABLE_ROAD_MAP_WIDTH_TILES] != 9u ||
            window[17u * PORTABLE_ROAD_MAP_WIDTH_TILES] != 9u)
            return 1;
        memcpy(saved,repack_map,sizeof saved);
        memcpy(first_window,window,sizeof window);
        if(portable_road_map_asset_repack(&world,&window_buffers,1400*65536,1600*65536,
            16384,sine,1024,&repacked) ||
           portable_road_map_asset_repack(&world,&window_buffers,80*524288,80*524288,
            0,sine,1024,&repacked) || memcmp(saved,repack_map,sizeof saved) ||
           memcmp(first_window,window,sizeof window))return 1;
        PortableRoadMapBuffers alias={repack_map,sizeof repack_map};
        if(portable_road_map_asset_repack(&world,&alias,0,0,0,sine,1024,&repacked)!=
            PORTABLE_ROAD_MAP_INVALID_ARGUMENT)return 1;
        /* Nonzero cardinal sine catches the old 0x400 instead of 0x4000 turn. */
        sine[256]=32768;sine[768]=-32768;
        if(portable_road_map_asset_repack(&world,&window_buffers,80*524288,80*524288,
            0,sine,1024,&repacked) || window[0]!=1 || window[127*128]!=0)return 1;

    }

    puts("portable road map: PASS");
    return 0;
}

int run_road_palette(void)
{
    static uint8_t rom[0x80];
    static uint16_t palette[PORTABLE_ROAD_PALETTE_FIXED_COUNT];

    memset(rom, 0, sizeof(rom));
    memset(palette, 0, sizeof(palette));
    for (size_t index = 0; index < PORTABLE_ROAD_PALETTE_FIXED_COUNT; ++index)
        write_le16(rom + 0x20u + index * 2u,
                   (uint16_t)(UINT16_C(0x1200) + index));
    if (portable_road_palette_load_fixed(
            rom, sizeof(rom), 0x20, palette,
            PORTABLE_ROAD_PALETTE_FIXED_COUNT) != PORTABLE_ROAD_PALETTE_OK ||
        palette[0] != 0x1200 || palette[15] != 0x120f ||
        portable_road_palette_load_fixed(
            rom, sizeof(rom), 0x20, palette,
            PORTABLE_ROAD_PALETTE_FIXED_COUNT - 1u) !=
            PORTABLE_ROAD_PALETTE_CAPACITY ||
        portable_road_palette_load_fixed(
            rom, 0x2f, 0x20, palette,
            PORTABLE_ROAD_PALETTE_FIXED_COUNT) !=
            PORTABLE_ROAD_PALETTE_OUT_OF_BOUNDS ||
        portable_road_palette_load_fixed(
            NULL, sizeof(rom), 0x20, palette,
            PORTABLE_ROAD_PALETTE_FIXED_COUNT) !=
            PORTABLE_ROAD_PALETTE_INVALID_ARGUMENT)
        return 1;

    puts("portable road palette: PASS");
    return 0;
}

int run_source_motion_tables(void)
{
    static uint8_t rom[PORTABLE_SOURCE_STEERING_DELTA_OFFSET +
                       PORTABLE_SOURCE_STEERING_DELTA_BYTES];
    static int32_t sine[PORTABLE_SOURCE_SINE_COUNT];
    static int16_t auto_acceleration[PORTABLE_SOURCE_AUTO_ACCELERATION_COUNT];
    static int16_t brake[PORTABLE_SOURCE_BRAKE_COUNT];
    static int16_t brake_accelerate[PORTABLE_SOURCE_BRAKE_ACCELERATE_COUNT];
    static int16_t forward_response[PORTABLE_SOURCE_FORWARD_RESPONSE_COUNT];
    static int16_t steering_response[PORTABLE_SOURCE_STEERING_RESPONSE_COUNT];
    static int16_t steering_delta[PORTABLE_SOURCE_STEERING_DELTA_COUNT];
    static int16_t local_table[PORTABLE_SOURCE_LOCAL_TABLE_COUNT];
    static int16_t forward_thresholds[PORTABLE_SOURCE_FORWARD_THRESHOLD_COUNT];
    static const uint16_t local_source[PORTABLE_SOURCE_LOCAL_TABLE_COUNT] = {
        0x0060, 0x00a0, 0x00c0, 0x00e0, 0x00d0, 0x00c0, 0x00a0,
        0x00a0, 0x0080, 0x0060, 0x0040, 0x0020, 0x0020, 0x0010,
        0x0008, 0x0008,
    };
    PortableSourceMotionTableBuffers buffers = {
        .sine = sine,
        .sine_capacity = PORTABLE_SOURCE_SINE_COUNT,
        .auto_acceleration = auto_acceleration,
        .auto_acceleration_capacity = PORTABLE_SOURCE_AUTO_ACCELERATION_COUNT,
        .brake = brake,
        .brake_capacity = PORTABLE_SOURCE_BRAKE_COUNT,
        .brake_accelerate = brake_accelerate,
        .brake_accelerate_capacity = PORTABLE_SOURCE_BRAKE_ACCELERATE_COUNT,
        .forward_response = forward_response,
        .forward_response_capacity = PORTABLE_SOURCE_FORWARD_RESPONSE_COUNT,
        .steering_delta = steering_delta,
        .steering_delta_capacity = PORTABLE_SOURCE_STEERING_DELTA_COUNT,
    };
    PortableSourceMotionTableView view;

    memset(rom, 0, sizeof(rom));
    memset(sine, 0, sizeof(sine));
    memset(auto_acceleration, 0, sizeof(auto_acceleration));
    memset(brake, 0, sizeof(brake));
    memset(brake_accelerate, 0, sizeof(brake_accelerate));
    memset(forward_response, 0, sizeof(forward_response));
    memset(steering_response, 0, sizeof(steering_response));
    memset(steering_delta, 0, sizeof(steering_delta));
    memset(local_table, 0, sizeof(local_table));
    memset(forward_thresholds, 0, sizeof(forward_thresholds));
    write_le32(rom + PORTABLE_SOURCE_SINE_OFFSET, 0x12345678u);
    write_le32(rom + PORTABLE_SOURCE_SINE_OFFSET + 256u * 4u,
               0x80000000u);
    write_le16(rom + PORTABLE_SOURCE_AUTO_ACCELERATION_OFFSET + 24u * 2u,
               0xfffeu);
    write_le16(rom + PORTABLE_SOURCE_BRAKE_OFFSET, 0xfffcu);
    write_le16(rom + PORTABLE_SOURCE_BRAKE_ACCELERATE_OFFSET + 7u * 2u,
               0xff00u);
    write_le16(rom + PORTABLE_SOURCE_FORWARD_RESPONSE_OFFSET + 1u * 2u,
               0xff80u);
    write_le16(rom + PORTABLE_SOURCE_STEERING_DELTA_OFFSET + 8u * 2u,
               0x8000u);
    write_le16(rom + PORTABLE_SOURCE_STEERING_RESPONSE_OFFSET +
                   3u * PORTABLE_SOURCE_STEERING_RESPONSE_CLASS_STRIDE,
               0x09aeu);
    write_le16(rom + PORTABLE_SOURCE_STEERING_RESPONSE_OFFSET +
                   3u * PORTABLE_SOURCE_STEERING_RESPONSE_CLASS_STRIDE + 2u,
               0x009fu);
    write_le16(rom + PORTABLE_SOURCE_STEERING_RESPONSE_OFFSET +
                   3u * PORTABLE_SOURCE_STEERING_RESPONSE_CLASS_STRIDE + 4u,
               0x006cu);
    write_le16(rom + PORTABLE_SOURCE_STEERING_RESPONSE_OFFSET +
                   3u * PORTABLE_SOURCE_STEERING_RESPONSE_CLASS_STRIDE + 6u,
               0x0094u);
    for (size_t index = 0; index < PORTABLE_SOURCE_LOCAL_TABLE_COUNT;
         ++index)
        write_le16(rom + PORTABLE_SOURCE_LOCAL_TABLE_OFFSET +
                       3u * PORTABLE_SOURCE_LOCAL_TABLE_CLASS_STRIDE +
                       index * 2u,
                   local_source[index]);
    {
        static const uint16_t source[6] = {33,16,30,30,44,48};
        for(size_t index=0;index<6;index++)
            write_le16(rom+PORTABLE_SOURCE_FORWARD_THRESHOLD_OFFSET+
                (2u*6u+index)*2u,source[index]);
    }
    if (portable_source_motion_tables_build(
            rom, sizeof(rom), &buffers, &view) !=
            PORTABLE_SOURCE_MOTION_TABLES_OK ||
        view.sine != sine || view.sine_count != PORTABLE_SOURCE_SINE_COUNT ||
        view.auto_acceleration != auto_acceleration ||
        view.auto_acceleration_count != PORTABLE_SOURCE_AUTO_ACCELERATION_COUNT ||
        view.brake != brake || view.brake_count != PORTABLE_SOURCE_BRAKE_COUNT ||
        view.brake_accelerate != brake_accelerate ||
        view.brake_accelerate_count != PORTABLE_SOURCE_BRAKE_ACCELERATE_COUNT ||
        view.forward_response != forward_response ||
        view.forward_response_count != PORTABLE_SOURCE_FORWARD_RESPONSE_COUNT ||
        view.steering_delta != steering_delta ||
        view.steering_delta_count != PORTABLE_SOURCE_STEERING_DELTA_COUNT ||
        sine[0] != INT32_C(0x12345678) || sine[256] != INT32_MIN ||
        auto_acceleration[24] != -2 || brake[0] != -4 ||
        brake_accelerate[7] != -256 || forward_response[1] != -128 ||
        steering_delta[8] != INT16_MIN)
        return 1;
    if (portable_source_steering_response_build(
            rom, sizeof(rom), 3, 0, steering_response,
            PORTABLE_SOURCE_STEERING_RESPONSE_COUNT) !=
            PORTABLE_SOURCE_MOTION_TABLES_OK ||
        steering_response[0] != 0x09ae || steering_response[1] != 0x009f ||
        steering_response[2] != 0x006c || steering_response[3] != 0x0094 ||
        portable_source_steering_response_build(
            rom, sizeof(rom), 3, 0, steering_response,
            PORTABLE_SOURCE_STEERING_RESPONSE_COUNT - 1u) !=
            PORTABLE_SOURCE_MOTION_TABLES_CAPACITY ||
        portable_source_steering_response_build(
            rom, sizeof(rom), 3, 4, steering_response,
            PORTABLE_SOURCE_STEERING_RESPONSE_COUNT) !=
            PORTABLE_SOURCE_MOTION_TABLES_CAPACITY ||
        portable_source_steering_response_build(
            rom, PORTABLE_SOURCE_STEERING_RESPONSE_OFFSET, 3, 0,
            steering_response, PORTABLE_SOURCE_STEERING_RESPONSE_COUNT) !=
            PORTABLE_SOURCE_MOTION_TABLES_OUT_OF_BOUNDS ||
        portable_source_steering_response_build(
            NULL, sizeof(rom), 3, 0, steering_response,
            PORTABLE_SOURCE_STEERING_RESPONSE_COUNT) !=
            PORTABLE_SOURCE_MOTION_TABLES_INVALID_ARGUMENT)
        return 1;
    if (portable_source_local_table_build(
            rom, sizeof(rom), 3, 2, local_table,
            PORTABLE_SOURCE_LOCAL_TABLE_COUNT) !=
            PORTABLE_SOURCE_MOTION_TABLES_OK ||
        local_table[0] != 0x0900 || local_table[3] != 0x1500 ||
        local_table[15] != 0x00c0)
        return 1;
    if(portable_source_forward_thresholds_build(
           rom,sizeof(rom),2,2,forward_thresholds,
           PORTABLE_SOURCE_FORWARD_THRESHOLD_COUNT)!=
           PORTABLE_SOURCE_MOTION_TABLES_OK ||
       forward_thresholds[0]!=-1 || forward_thresholds[9]!=-1 ||
       forward_thresholds[10]!=688 || forward_thresholds[11]!=416 ||
       forward_thresholds[12]!=640 || forward_thresholds[13]!=640 ||
       forward_thresholds[14]!=864 || forward_thresholds[15]!=928 ||
       portable_source_forward_thresholds_build(
           rom,sizeof(rom),2,2,forward_thresholds,
           PORTABLE_SOURCE_FORWARD_THRESHOLD_COUNT-1)!=
           PORTABLE_SOURCE_MOTION_TABLES_CAPACITY ||
       portable_source_forward_thresholds_build(
           NULL,sizeof(rom),2,2,forward_thresholds,
           PORTABLE_SOURCE_FORWARD_THRESHOLD_COUNT)!=
           PORTABLE_SOURCE_MOTION_TABLES_INVALID_ARGUMENT)
        return 1;
    write_le16(rom + PORTABLE_SOURCE_LOCAL_TABLE_OFFSET +
                   3u * PORTABLE_SOURCE_LOCAL_TABLE_CLASS_STRIDE,
               0x0000u);
    if (portable_source_local_table_build(
            rom, sizeof(rom), 3, 2, local_table,
            PORTABLE_SOURCE_LOCAL_TABLE_COUNT) !=
            PORTABLE_SOURCE_MOTION_TABLES_OK ||
        local_table[0] != 0x00e0 ||
        portable_source_local_table_build(
            rom, sizeof(rom), 3, 2, local_table,
            PORTABLE_SOURCE_LOCAL_TABLE_COUNT - 1u) !=
            PORTABLE_SOURCE_MOTION_TABLES_CAPACITY ||
        portable_source_local_table_build(
            rom, PORTABLE_SOURCE_LOCAL_TABLE_OFFSET +
                    PORTABLE_SOURCE_LOCAL_TABLE_CLASS_STRIDE * 3u + 31u,
            3, 2, local_table, PORTABLE_SOURCE_LOCAL_TABLE_COUNT) !=
            PORTABLE_SOURCE_MOTION_TABLES_OUT_OF_BOUNDS ||
        portable_source_local_table_build(
            NULL, sizeof(rom), 3, 2, local_table,
            PORTABLE_SOURCE_LOCAL_TABLE_COUNT) !=
            PORTABLE_SOURCE_MOTION_TABLES_INVALID_ARGUMENT)
        return 1;

    buffers.sine_capacity = PORTABLE_SOURCE_SINE_COUNT - 1u;
    if (portable_source_motion_tables_build(
            rom, sizeof(rom), &buffers, &view) !=
        PORTABLE_SOURCE_MOTION_TABLES_CAPACITY)
        return 1;
    buffers.sine_capacity = PORTABLE_SOURCE_SINE_COUNT;
    if (portable_source_motion_tables_build(
            rom, PORTABLE_SOURCE_STEERING_DELTA_OFFSET, &buffers, &view) !=
        PORTABLE_SOURCE_MOTION_TABLES_OUT_OF_BOUNDS ||
        portable_source_motion_tables_build(
            NULL, sizeof(rom), &buffers, &view) !=
            PORTABLE_SOURCE_MOTION_TABLES_INVALID_ARGUMENT)
        return 1;

    puts("portable source motion tables: PASS");
    return 0;
}

int run_source_b550_tables(void)
{
    static uint8_t rom[PORTABLE_SOURCE_B550_Y_OFFSET +
                       PORTABLE_SOURCE_B550_BYTES];
    static int16_t x_table[PORTABLE_SOURCE_B550_COUNT];
    static int16_t y_table[PORTABLE_SOURCE_B550_COUNT];
    const PortableSourceB550TableBuffers buffers = {
        .x_components = x_table,
        .x_capacity = PORTABLE_SOURCE_B550_COUNT,
        .y_components = y_table,
        .y_capacity = PORTABLE_SOURCE_B550_COUNT,
    };
    PortableSourceB550TableView view;
    PortableDriverB550Resultant result;
    int16_t out_x;
    int16_t out_y;

    memset(rom, 0, sizeof(rom));
    write_le16(rom + PORTABLE_SOURCE_B550_X_OFFSET + 0u, 0x0080u);
    write_le16(rom + PORTABLE_SOURCE_B550_X_OFFSET + 2u, 0x0080u);
    write_le16(rom + PORTABLE_SOURCE_B550_X_OFFSET + 4u, 0x00f0u);
    write_le16(rom + PORTABLE_SOURCE_B550_X_OFFSET + 6u, 0x00f0u);
    write_le16(rom + PORTABLE_SOURCE_B550_Y_OFFSET + 0u, 0x00f0u);
    write_le16(rom + PORTABLE_SOURCE_B550_Y_OFFSET + 2u, 0x00f0u);
    write_le16(rom + PORTABLE_SOURCE_B550_Y_OFFSET + 4u, 0x0080u);
    write_le16(rom + PORTABLE_SOURCE_B550_Y_OFFSET + 6u, 0x0080u);
    if (portable_source_b550_tables_build(
            rom, sizeof(rom), &buffers, &view) !=
            PORTABLE_SOURCE_B550_TABLES_OK ||
        view.x_components != x_table || view.y_components != y_table ||
        view.x_count != PORTABLE_SOURCE_B550_COUNT ||
        view.y_count != PORTABLE_SOURCE_B550_COUNT || x_table[0] != 128 ||
        x_table[2] != 240 || y_table[0] != 240 || y_table[2] != 128)
        return 1;

    if (portable_driver_b550_normal_resultant(
            654, 64, INT32_C(0x02920000), 0, x_table,
            PORTABLE_SOURCE_B550_COUNT, y_table,
            PORTABLE_SOURCE_B550_COUNT, &out_x, &out_y, &result) !=
            PORTABLE_DRIVER_B550_RESULTANT_OK ||
        out_x != 327 || out_y != 60 ||
        result.branch != PORTABLE_DRIVER_B550_BRANCH_TABLE ||
        result.direction_index != 0)
        return 1;
    if (portable_driver_b550_normal_resultant(
            -654, -64, INT32_C(0x02920000), 0, x_table,
            PORTABLE_SOURCE_B550_COUNT, y_table,
            PORTABLE_SOURCE_B550_COUNT, &out_x, &out_y, &result) !=
            PORTABLE_DRIVER_B550_RESULTANT_OK ||
        out_x != -327 || out_y != -60)
        return 1;
    if (portable_driver_b550_normal_resultant(
            654, -64, INT32_C(0x05000000), 0, x_table,
            PORTABLE_SOURCE_B550_COUNT, y_table,
            PORTABLE_SOURCE_B550_COUNT, &out_x, &out_y, &result) !=
            PORTABLE_DRIVER_B550_RESULTANT_OK ||
        out_x != 163 || out_y != -16 ||
        result.branch != PORTABLE_DRIVER_B550_BRANCH_QUARTER)
        return 1;
    if (portable_driver_b550_normal_resultant(
            -1, 1, 0, 0, x_table, PORTABLE_SOURCE_B550_COUNT, y_table,
            PORTABLE_SOURCE_B550_COUNT, &out_x, &out_y, &result) !=
            PORTABLE_DRIVER_B550_RESULTANT_OK ||
        out_x != -256 || out_y != 256 ||
        result.branch != PORTABLE_DRIVER_B550_BRANCH_CLAMP)
        return 1;
    if (portable_driver_b550_normal_resultant(
            654, 64, INT32_C(0x02920000), 4, x_table,
            PORTABLE_SOURCE_B550_COUNT, y_table,
            PORTABLE_SOURCE_B550_COUNT, &out_x, &out_y, &result) !=
            PORTABLE_DRIVER_B550_RESULTANT_DIRECTION_OUT_OF_RANGE ||
        portable_driver_b550_normal_resultant(
            654, 64, INT32_C(0x02920000), 0, NULL, 0, y_table,
            PORTABLE_SOURCE_B550_COUNT, &out_x, &out_y, &result) !=
            PORTABLE_DRIVER_B550_RESULTANT_DIRECTION_OUT_OF_RANGE ||
        portable_source_b550_tables_build(
            rom, PORTABLE_SOURCE_B550_X_OFFSET, &buffers, &view) !=
            PORTABLE_SOURCE_B550_TABLES_OUT_OF_BOUNDS ||
        portable_source_b550_tables_build(
            NULL, sizeof(rom), &buffers, &view) !=
            PORTABLE_SOURCE_B550_TABLES_INVALID_ARGUMENT)
        return 1;

    /* Source signed-halfword absolute comparison: -32768 wraps back to
     * -32768, so a small other component selects the clamp path. */
    if (portable_driver_b550_normal_resultant(
            INT16_MIN, 1, 0, 0, x_table, PORTABLE_SOURCE_B550_COUNT,
            y_table, PORTABLE_SOURCE_B550_COUNT, &out_x, &out_y, &result) !=
            PORTABLE_DRIVER_B550_RESULTANT_OK || out_x != -256 || out_y != 256 ||
        result.branch != PORTABLE_DRIVER_B550_BRANCH_CLAMP)
        return 1;

    {
        PortableDriverB550Flags flags = {UINT32_C(0x8400), 1, 1};
        if (portable_driver_b550_finish_flags(0x8001, 0, &flags) != 0 ||
            flags.flags != 0xc400 || flags.timer_38 != 1 || flags.timer_62 != 1)
            return 1;
        if (portable_driver_b550_finish_flags(0, 1, &flags) != 0 ||
            flags.flags != 0xd400 || flags.timer_38 != 0 || flags.timer_62 != 1)
            return 1;
        if (portable_driver_b550_finish_flags(0, 0, &flags) != 0 ||
            flags.flags != 0xd400 || flags.timer_62 != 0)
            return 1;
        if (portable_driver_b550_finish_flags(0, 0, &flags) != 0 ||
            flags.flags != 0x9400)
            return 1;
        if (portable_driver_b550_finish_flags(0, 2, &flags) !=
                PORTABLE_DRIVER_B550_RESULTANT_INVALID_ARGUMENT ||
            flags.flags != 0x9400 ||
            portable_driver_b550_finish_flags(0, 0, NULL) !=
                PORTABLE_DRIVER_B550_RESULTANT_INVALID_ARGUMENT)
            return 1;
    }

    puts("portable source b550 tables: PASS");
    return 0;
}

int run_terrain_effect_tables(void)
{
    static uint8_t rom[PORTABLE_TERRAIN_EFFECT_Y_OFFSET +
                       PORTABLE_TERRAIN_EFFECT_OFFSET_BYTES];
    static int32_t x_offsets[PORTABLE_TERRAIN_EFFECT_OFFSET_COUNT];
    static int32_t y_offsets[PORTABLE_TERRAIN_EFFECT_OFFSET_COUNT];
    static const uint32_t expected_x[8] = {
        0x00000000u, 0x00000008u, 0x00000000u, 0xfffffff8u,
        0x00000000u, 0xfffffff8u, 0x00000000u, 0x00000008u,
    };
    static const uint32_t expected_y[8] = {
        0x00000000u, 0xfffffff8u, 0x00000000u, 0xfffffff8u,
        0x00000000u, 0x00000008u, 0x00000000u, 0x00000008u,
    };
    PortableTerrainEffectTableBuffers buffers = {
        .x_offsets = x_offsets,
        .x_capacity = 8,
        .y_offsets = y_offsets,
        .y_capacity = 8,
    };
    PortableTerrainEffectTableView view;
    size_t index;

    memset(rom, 0, sizeof(rom));
    for (index = 0; index < 8; ++index)
    {
        write_le32(rom + PORTABLE_TERRAIN_EFFECT_X_OFFSET + index * 4u,
                   expected_x[index]);
        write_le32(rom + PORTABLE_TERRAIN_EFFECT_Y_OFFSET + index * 4u,
                   expected_y[index]);
    }
    if (portable_terrain_effect_tables_build(
            rom, sizeof(rom), &buffers, &view) !=
            PORTABLE_TERRAIN_EFFECT_TABLES_OK ||
        view.x_offsets != x_offsets || view.y_offsets != y_offsets ||
        view.x_count != 8 || view.y_count != 8)
        return 1;
    for (index = 0; index < 8; ++index)
    {
        if ((uint32_t)x_offsets[index] != expected_x[index] ||
            (uint32_t)y_offsets[index] != expected_y[index])
            return 1;
    }
    buffers.x_capacity = 7;
    if (portable_terrain_effect_tables_build(
            rom, sizeof(rom), &buffers, &view) !=
        PORTABLE_TERRAIN_EFFECT_TABLES_CAPACITY)
        return 1;
    buffers.x_capacity = 8;
    if (portable_terrain_effect_tables_build(
            rom, PORTABLE_TERRAIN_EFFECT_Y_OFFSET, &buffers, &view) !=
        PORTABLE_TERRAIN_EFFECT_TABLES_OUT_OF_BOUNDS ||
        portable_terrain_effect_tables_build(
            NULL, sizeof(rom), &buffers, &view) !=
        PORTABLE_TERRAIN_EFFECT_TABLES_INVALID_ARGUMENT)
        return 1;

    puts("portable terrain effect tables: PASS");
    return 0;
}
