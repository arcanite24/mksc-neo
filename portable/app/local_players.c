#include "local_players.h"

#include <stddef.h>

unsigned portable_local_players_max(unsigned pad_count)
{
    return pad_count<2u?2u:pad_count>PORTABLE_LOCAL_PLAYERS_MAX?PORTABLE_LOCAL_PLAYERS_MAX:pad_count;
}

int portable_local_players_assign(unsigned players,unsigned pad_count,
    PortableLocalInput inputs[PORTABLE_LOCAL_PLAYERS_MAX])
{
    if(inputs==NULL||players<1u||players>portable_local_players_max(pad_count))return 0;
    for(unsigned player=0u;player<PORTABLE_LOCAL_PLAYERS_MAX;++player)
        inputs[player]=(PortableLocalInput){
            (uint8_t)(player<players&&player<2u?player+1u:0u),
            (int8_t)(player<players&&player<pad_count?(int)player:-1)};
    return 1;
}

void portable_local_players_buttons(const PortableLocalInput *inputs,unsigned players,
    uint16_t keyboard1,uint16_t keyboard2,const uint16_t pads[PORTABLE_LOCAL_PLAYERS_MAX],
    uint16_t buttons[PORTABLE_LOCAL_PLAYERS_MAX])
{
    if(buttons==NULL)return;
    for(unsigned player=0u;player<PORTABLE_LOCAL_PLAYERS_MAX;++player){
        uint16_t value=0u;
        if(inputs!=NULL&&player<players){
            const PortableLocalInput *input=&inputs[player];
            if(input->keyboard==1u)value|=keyboard1;
            if(input->keyboard==2u)value|=keyboard2;
            if(pads!=NULL&&input->pad_slot>=0&&(unsigned)input->pad_slot<PORTABLE_LOCAL_PLAYERS_MAX)
                value|=pads[input->pad_slot];
        }
        buttons[player]=value;
    }
}
