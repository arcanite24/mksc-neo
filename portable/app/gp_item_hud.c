#include "gp_item_hud.h"

#include <stddef.h>

typedef struct
{
    uint16_t item;
    PortableGpItemHudSelection graphic;
} SourceGpItemGraphicEntry;

int portable_gp_item_hud_selection(uint16_t item,
                                 PortableGpItemHudSelection *out)
{
    /* Exact sub_804CD98 cases. Logical pages 15/16 are remapped by the
       source DMA path to physical pages 11/10. Green uses logical pages
       11/10, red uses 15/16; the HUD content owns their distinct palettes. */
    static const SourceGpItemGraphicEntry graphics[] = {
        {0x0002u, {13u,0u}},
        {0x1000u, {11u,0u}}, {0x1001u, {10u,0u}},
        {0x1002u, {15u,0u}}, {0x1003u, {16u,0u}},
        {0x1004u, {9u,0u}}, {0x1005u, {1u,0u}},
        {0x1006u, {8u,0u}}, {0x1007u, {6u,0u}},
        {0x1008u, {4u,0u}}, {0x1009u, {7u,0u}},
        {0x100au, {3u,0u}}, {0x100bu, {13u,0u}},
        {0x100cu, {2u,0u}}, {0x100du, {12u,0u}},
        {0x100eu, {7u,0u}}, {0x100fu, {5u,0u}},
        {0x2000u, {11u,0u}}, {0x2001u, {10u,0u}},
        {0x2002u, {15u,0u}}, {0x2003u, {16u,0u}},
        {0x2004u, {9u,0u}}, {0x2005u, {1u,0u}},
        {0x2007u, {6u,0u}}, {0x200au, {3u,0u}},
        {0x200cu, {2u,0u}},
    };
    if (out == NULL)
        return 0;
    if (item == 0u) {
        *out = (PortableGpItemHudSelection){0u,0u};
        return 1;
    }
    for (size_t index = 0u; index < sizeof graphics / sizeof graphics[0];
         ++index)
    {
        if (graphics[index].item == item)
        {
            *out = graphics[index].graphic;
            out->physical_page=out->logical_page==15u?11u:out->logical_page==16u?10u:out->logical_page;
            return 1;
        }
    }
    return 0;
}

void portable_gp_item_hud_transition(PortableGpItemHud *state,
    uint16_t previous,uint16_t current)
{
    state->previous=previous;
    state->current=current;
    state->remaining=160u;
    state->displayed_item=current;
}

void portable_gp_item_hud_consume(PortableGpItemHud *state)
{
    /* D3E0 preserves the predecessor while triple mushrooms count down. */
    if(state->current==0x1008u || state->current==0x100fu)
        portable_gp_item_hud_transition(state,state->current,
            state->current==0x1008u?0x100fu:0x1007u);
    else *state=(PortableGpItemHud){0};
}

uint16_t portable_gp_item_hud_tick(PortableGpItemHud *state,uint8_t finished)
{
    uint16_t song=0u;
    if(finished)return 0u;
    const uint16_t timer=state->remaining;
    if(timer==0u){
        if(state->current==2u)state->current=0u;
        state->displayed_item=state->current;
        return 0u;
    }
    if((timer&7u)==0u){
        const uint16_t old=state->previous,current=state->current;
        if((timer&8u)==0u){
            if(timer>64u && old!=0x1008u && old!=0x100fu && old!=0x100bu && current!=2u)
                song=104u;
            state->displayed_item=current;
        }else if(old==0x1008u || old==0x100fu || old==0x100bu || current==2u){
            state->displayed_item=old==0x100bu && (current==2u || current==old)?0u:old;
            if(old==0x100bu && current!=0u)song=130u;
            else if(current==2u)song=131u;
        }else state->displayed_item=0u;
    }
    --state->remaining;
    return song;
}
