#include "item_box_asset.h"

#include <stdio.h>
#include <string.h>

static void put32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0u; i < 4u; ++i) p[i] = (uint8_t)(value >> (8u * i));
}

static void fixture(uint8_t *bytes)
{
    uint32_t ids[7];
    for(unsigned i=0u;i<7u;++i)ids[i]=0x100u+i*7u;
    const unsigned counts[7] = {2u,3u,1u,3u,2u,3u,2u};
    const uint32_t header[8] = {0x31504249u,7u,7u,8u,3u,110u,3072u,0u};
    size_t offset = 172u;
    memset(bytes, 0, PORTABLE_ITEM_BOX_RESOURCE_BYTES);
    for (unsigned i = 0u; i < 8u; ++i) put32(bytes + i * 4u, header[i]);
    for (unsigned i = 0u; i < 7u; ++i) {
        const unsigned length = 2u + 6u * counts[i];
        put32(bytes + 32u + 12u * i, ids[i]);
        put32(bytes + 36u + 12u * i, (uint32_t)offset);
        put32(bytes + 40u + 12u * i, length);
        bytes[offset] = (uint8_t)counts[i];
        for (unsigned j = 0u; j < counts[i]; ++j) {
            /* Valid synthetic cells, independently sized for 32x32 normal
             * and 16x16 shards. No ROM pixels are embedded in this test. */
            bytes[offset + 2u + 6u * j] = 0xf0u;
            bytes[offset + 4u + 6u * j] = 0xf0u;
            bytes[offset + 5u + 6u * j] = i == 0u ? 0x81u : 0x41u;
            bytes[offset + 6u + 6u * j] = i == 0u ? 0x60u : 0x70u;
            bytes[offset + 7u + 6u * j] = 0x65u;
        }
        put32(bytes + 116u + i * 8u, i < 6u ? ids[i + 1u] : 0u);
        put32(bytes + 120u + i * 8u, 3u+i*2u);
        offset += length;
    }
    memset(bytes + offset, 0xf6, 3072u);
}

static int rejects_unchanged(const uint8_t *bytes, size_t size,
                             PortableItemBoxResourcesView *view)
{
    PortableItemBoxResourcesView before = *view;
    return !portable_item_box_resources_read(bytes, size, view) &&
        memcmp(&before, view, sizeof before) == 0;
}

int main(void)
{
    uint8_t storage[PORTABLE_ITEM_BOX_RESOURCE_BYTES + 1u];
    uint8_t corrupt[PORTABLE_ITEM_BOX_RESOURCE_BYTES];
    uint8_t *bytes = storage + 1u; /* Little-endian reads must allow misalignment. */
    PortableItemBoxResourcesView view = {0};
    fixture(bytes);
    if (!portable_item_box_resources_read(bytes, PORTABLE_ITEM_BOX_RESOURCE_BYTES, &view) ||
        view.cells[0] != bytes + 172u || view.cells[6] != bytes + 268u ||
        view.sequence != bytes + 116u || view.shard_tiles != bytes + 282u ||
        portable_item_box_resource_cell(&view, 0x100u) != view.cells[0] ||
        portable_item_box_resource_cell(&view, 0x12au) != view.cells[6] ||
        portable_item_box_resource_cell(&view, 0x03000000u) != NULL ||
        portable_item_box_resource_cell(NULL, 0x100u) != NULL) return 1;
    for (size_t size = 0u; size < PORTABLE_ITEM_BOX_RESOURCE_BYTES; ++size)
        if (!rejects_unchanged(bytes, size, &view)) return 1;
    if (!rejects_unchanged(NULL, PORTABLE_ITEM_BOX_RESOURCE_BYTES, &view) ||
        !rejects_unchanged(bytes, PORTABLE_ITEM_BOX_RESOURCE_BYTES + 1u, &view) ||
        portable_item_box_resources_read(bytes, PORTABLE_ITEM_BOX_RESOURCE_BYTES, NULL)) return 1;
    /* Bad directory offset/length, duplicate ID, unknown script reference,
     * unterminated script, zero duration and unsupported geometry. */
    const size_t offsets[] = {36u,40u,44u,116u,164u,120u,175u,177u,178u,28u};
    const uint8_t values[] = {0xffu,0xffu,0u,0xeeu,1u,0u,0x40u,0x01u,0x7fu,1u};
    for (unsigned i = 0u; i < sizeof offsets / sizeof offsets[0]; ++i) {
        memcpy(corrupt, bytes, sizeof corrupt);
        corrupt[offsets[i]] = values[i];
        if (!rejects_unchanged(corrupt, sizeof corrupt, &view)) {
            fprintf(stderr, "malformed resource accepted at byte %zu\n", offsets[i]);
            return 1;
        }
    }
    /* Directory IDs, animation order/durations and texels are content.
     * Renaming one cell and its reference must preserve the borrowed view. */
    put32(bytes+44u,0x234567u);put32(bytes+116u,0x234567u);
    put32(bytes+120u,23u);bytes[282u]=0x87u;
    if(!portable_item_box_resources_read(bytes,PORTABLE_ITEM_BOX_RESOURCE_BYTES,&view) ||
       portable_item_box_resource_cell(&view,0x234567u)!=view.cells[1] ||
       portable_item_box_resource_cell(&view,0x107u)!=NULL ||
       view.sequence[4]!=23u || view.shard_tiles[0]!=0x87u)return 1;
    puts("PASS item-box resource bounds, references, geometry, unaligned data and atomic failure");
    return 0;
}
