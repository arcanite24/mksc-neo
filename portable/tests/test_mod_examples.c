/* The ROM-free example mods, packaged by tools/portable/mod_kit.py as .zip
 * files, load through the real loader on a synthetic base pack: JSON data
 * applies and every Lua script compiles in the sandbox. */
#include "mod_loader.h"
#include "script_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do{if(!(x)){fprintf(stderr,"mod examples line %d: %s\n",__LINE__,#x);exit(1);}}while(0)

int main(int argc,char **argv)
{
    CHECK(argc==3);
    FILE *file=fopen(argv[1],"rb");CHECK(file!=NULL&&fseek(file,0,SEEK_END)==0);
    const long length=ftell(file);CHECK(length>0&&fseek(file,0,SEEK_SET)==0);
    uint8_t *bytes=malloc((size_t)length);CHECK(bytes!=NULL&&fread(bytes,1u,(size_t)length,file)==(size_t)length);
    fclose(file);
    PortableAssetPackView base;CHECK(portable_asset_pack_open(bytes,(size_t)length,&base)==PORTABLE_ASSET_PACK_OK);
    char ids[4][PORTABLE_MOD_ID_CAPACITY]={"data-tuning","lucky-start","lap-bonus","item-swap"};
    PortableModSet *mods=portable_mods_load(&base,argv[2],(const char (*)[PORTABLE_MOD_ID_CAPACITY])ids,4u);
    CHECK(mods!=NULL&&portable_mods_loaded_count(mods)==4u);
    const PortableModIdentity *identity=portable_mods_identity(mods);
    for(unsigned i=0u;i<4u;++i)CHECK(strcmp(identity->mods[i].id,ids[i])==0&&strcmp(identity->mods[i].version,"1.0.0")==0);
    PortableAssetSectionView points;
    CHECK(portable_asset_pack_find_section(portable_mods_pack(mods),59u,1u,&points)==PORTABLE_ASSET_PACK_OK&&
          points.size>=16u&&points.bytes[8]==18u&&points.bytes[9]==12u);
    PortableScriptSource sources[PORTABLE_MOD_LIMIT];size_t count=0u;
    for(size_t i=0u;i<portable_mods_loaded_count(mods);++i)
        if(portable_mods_script(mods,i,&sources[count].id,&sources[count].source,&sources[count].size))++count;
    CHECK(count==3u);
    PortableScriptHost *host=portable_script_host_create(sources,count);
    CHECK(host!=NULL&&portable_script_host_active_count(host)==3u);
    portable_script_host_free(host);portable_mods_free(mods);free(bytes);
    puts("mod examples: PASS data-tuning, lucky-start, lap-bonus and item-swap zips load and compile");
    return 0;
}
