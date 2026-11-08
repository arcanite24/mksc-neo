#ifndef PORTABLE_TEST_SHELL_CELLS_FIXTURE_H
#define PORTABLE_TEST_SHELL_CELLS_FIXTURE_H
#include <stdint.h>

/* Authored one-cell animations: every middle frame repeats the first art.
 * Separate sparse frame1/frame5 probes exercise offsets and reflection. */
static inline const uint8_t *shell_test_cells(void)
{
    static uint8_t bytes[304];
    for(unsigned group=0u;group<3u;++group)for(unsigned frame=0u;frame<6u;++frame){
        uint8_t *p=bytes+160u+(group*6u+frame)*8u;
        const uint16_t tile=(uint16_t)(group==2u?(frame==1u?0x110u:0x10cu):0x100u);
        const uint16_t values[4]={1u,(uint16_t)(group==2u&&frame==1u?0xf2u:0xf3u),
            (uint16_t)(frame==5u?0x51f8u:0x41f8u),
            (uint16_t)(((3u+group)<<12u)|0x400u|tile)};
        for(unsigned field=0u;field<4u;++field){
            p[field*2u]=(uint8_t)values[field];
            p[field*2u+1u]=(uint8_t)(values[field]>>8u);
        }
    }
    return bytes;
}
#endif
