#include "game_library.h"
#include <string.h>
static const PortableLibraryGame games[]={
    {"base","Super Circuit","GAME BOY ADVANCE",PORTABLE_GAME_BASE,0xff653db7u},
    {"mario-kart-ds","Mario Kart DS","NINTENDO DS",PORTABLE_GAME_IMPORT,0xffbd253du},
    {"mario-kart-64","Mario Kart 64","NINTENDO 64",PORTABLE_GAME_IMPORT,0xff287a58u},
};
size_t portable_game_library_count(void){return sizeof games/sizeof games[0];}
const PortableLibraryGame *portable_game_library_at(size_t i)
{return i<portable_game_library_count()?&games[i]:NULL;}
const PortableModCatalogEntry *portable_game_library_entry(const PortableModManager *manager,size_t index)
{
    const PortableLibraryGame *game=portable_game_library_at(index);
    const PortableModCatalog *catalog=portable_mod_manager_catalog(manager);
    for(size_t i=0;game&&catalog&&i<catalog->count;++i)
        if(!strcmp(game->id,catalog->entries[i].id))return &catalog->entries[i];
    return NULL;
}
