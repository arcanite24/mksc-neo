#include "source_normal_response_table.h"

#include <stdint.h>

int portable_source_normal_response_produce(
    uint8_t status,
    int16_t velocity,
    const PortableSourceNormalResponseTableView *table,
    PortableSourceNormalResponseResult *out)
{
    uint32_t magnitude;
    uint8_t index;

    if (table == NULL || out == NULL || table->entries == NULL)
        return PORTABLE_SOURCE_NORMAL_RESPONSE_TABLES_INVALID_ARGUMENT;
    if (table->count < PORTABLE_SOURCE_NORMAL_RESPONSE_COUNT)
        return PORTABLE_SOURCE_NORMAL_RESPONSE_TABLES_CAPACITY;

    *out = (PortableSourceNormalResponseResult){
        .next_status = status,
        .timer_cleared = 1,
        .output_written = 0,
        .table_index = 0,
        .output_q16 = 0,
    };
    if (status != UINT8_C(0x0b))
        return PORTABLE_SOURCE_NORMAL_RESPONSE_TABLES_OK;
    if (velocity == 0)
    {
        out->next_status = UINT8_C(0x0e);
        return PORTABLE_SOURCE_NORMAL_RESPONSE_TABLES_OK;
    }

    magnitude = velocity < 0 ? (uint32_t)(-(int32_t)velocity)
                              : (uint32_t)velocity;
    if (magnitude > UINT32_C(0x3fff))
        magnitude = UINT32_C(0x3f00);
    index = (uint8_t)((magnitude >> 11) & 7u);
    out->table_index = index;
    out->output_q16 = (int32_t)((uint32_t)table->entries[index] << 16);
    out->output_written = 1;
    out->timer_cleared = 0;
    return PORTABLE_SOURCE_NORMAL_RESPONSE_TABLES_OK;
}
