#ifndef MKSC_PORTABLE_CHARACTER_ASSET_H
#define MKSC_PORTABLE_CHARACTER_ASSET_H

#include <stddef.h>
#include <stdint.h>

#include "asset_pack.h"

#define PORTABLE_CHARACTER_FRAME_BYTES 0x800u
#define PORTABLE_CHARACTER_DIRECTION_FRAME_COUNT 29u
#define PORTABLE_CHARACTER_FULL_FRAME_COUNT 34u
#define PORTABLE_CHARACTER_LOD_FRAME_COUNT 25u
#define PORTABLE_CHARACTER_LOD32_FRAME_BYTES 0x200u
#define PORTABLE_CHARACTER_LOD16_FRAME_BYTES 0x80u
#define PORTABLE_CHARACTER_VIEW_SLOT_COUNT 64u
#define PORTABLE_CHARACTER_BLOCK_BYTES 0x16000u
#define PORTABLE_CHARACTER_PALETTE_STRIDE 0x200u
#define PORTABLE_CHARACTER_PALETTE_COUNT 16u
#define PORTABLE_CHARACTER_PALETTE_BANK_COUNT 16u
#define PORTABLE_CHARACTER_WIDTH 64u
#define PORTABLE_CHARACTER_HEIGHT 64u

typedef enum
{
    PORTABLE_CHARACTER_ASSET_OK = 0,
    PORTABLE_CHARACTER_ASSET_INVALID_ARGUMENT,
    PORTABLE_CHARACTER_ASSET_OUT_OF_BOUNDS,
    PORTABLE_CHARACTER_ASSET_CAPACITY,
    PORTABLE_CHARACTER_ASSET_FORMAT
} PortableCharacterAssetStatus;

typedef struct
{
    uint16_t *palette;
    size_t palette_capacity;
} PortableCharacterAssetBuffers;

typedef struct
{
    uint8_t tiles[PORTABLE_CHARACTER_FRAME_BYTES *
                  PORTABLE_CHARACTER_FULL_FRAME_COUNT];
    uint8_t lod32_tiles[PORTABLE_CHARACTER_LOD_FRAME_COUNT *
                        PORTABLE_CHARACTER_LOD32_FRAME_BYTES];
    uint8_t lod16_tiles[PORTABLE_CHARACTER_LOD_FRAME_COUNT *
                        PORTABLE_CHARACTER_LOD16_FRAME_BYTES];
    uint8_t view_frame_slots[PORTABLE_CHARACTER_VIEW_SLOT_COUNT];
    uint16_t palette[PORTABLE_CHARACTER_PALETTE_COUNT * PORTABLE_CHARACTER_PALETTE_BANK_COUNT];
} PortableCharacterPackedStorage;

typedef struct
{
    const uint8_t *tiles;
    size_t tile_bytes;
    const uint16_t *palette;
    size_t palette_count;
    const uint16_t *palette_banks;
    size_t palette_bank_count;
    size_t width;
    size_t height;
    uint32_t character_id;
    uint32_t frame_slot;
    size_t tile_offset;
    size_t palette_offset;
    const uint8_t *direction_tiles;
    size_t direction_tile_bytes;
    size_t direction_frame_count;
    const uint8_t *lod32_tiles;
    const uint8_t *lod16_tiles;
    const uint8_t *view_frame_slots;
    size_t view_frame_slot_count;
} PortableCharacterAssetView;

PortableCharacterAssetStatus portable_character_asset_build(
    const uint8_t *tile_data,
    size_t tile_data_size,
    const uint8_t *palette_data,
    size_t palette_data_size,
    uint32_t character_id,
    uint32_t frame_slot,
    const PortableCharacterAssetBuffers *buffers,
    PortableCharacterAssetView *out_view);

PortableCharacterAssetStatus portable_character_asset_open_packed(
    const PortableAssetSectionView *section,
    PortableCharacterPackedStorage *storage,
    PortableCharacterAssetView *out_view);

/* Version4 has a bounded variable frame count. Borrow byte-aligned tile/view
 * data from the pack and decode only the little-endian palette into caller
 * storage. The pack bytes must outlive the returned view. */
PortableCharacterAssetStatus portable_character_asset_open_borrowed(
    const PortableAssetSectionView *section,uint16_t *palette,size_t palette_capacity,
    PortableCharacterAssetView *out_view);

/* CD50 copies body colors3..15, preserving transparent/wheel colors0..2. */
int portable_character_asset_palette(const PortableCharacterAssetView *view,
    uint8_t bank,uint16_t output[PORTABLE_CHARACTER_PALETTE_COUNT]);

#endif
