#include "time_trial_ghost_exchange.h"
#include "time_trial_ghost_library.h"
#include "content_registry.h"
#include "recovered_pack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_temp.h"

#define CHECK(x) do{if(!(x)){fprintf(stderr,"ghost exchange line %d: %s\n",__LINE__,#x);return 1;}}while(0)

int main(int argc,char **argv)
{
    CHECK(argc==2);
    FILE *file=fopen(argv[1],"rb");CHECK(file&&fseek(file,0,SEEK_END)==0);
    long length=ftell(file);CHECK(length>0&&fseek(file,0,SEEK_SET)==0);
    uint8_t *bytes=malloc((size_t)length);
    PortableRecoveredPackStorage *storage=malloc(sizeof *storage);
    CHECK(bytes&&storage&&fread(bytes,1,(size_t)length,file)==(size_t)length&&fclose(file)==0);
    PortableAssetPackView pack;PortableContentSelection selection;PortableRecoveredRaceView race;
    CHECK(portable_asset_pack_open(bytes,(size_t)length,&pack)==PORTABLE_ASSET_PACK_OK);
    CHECK(portable_content_registry_select(&pack,0,0,&selection)==PORTABLE_CONTENT_OK);
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
    PortableRaceCompatibilityId identity=race.rules.compatibility;
    char directory[128],local[4096],incoming[4096],slots[2][4096];
    CHECK(test_temp_path(directory,sizeof directory,"mksc-exchange",1)==0);
    snprintf(local,sizeof local,"%s/course-%u-solo.ghost",directory,identity.course_id);
    snprintf(incoming,sizeof incoming,"%s/shared-course-%u-100.ghost",directory,identity.course_id);
    uint8_t record[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE],stored[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE];
    portable_time_trial_ghost_record_init(record,(uint8_t)identity.character_class,(uint8_t)identity.course_id);
    PortableTimeTrialGhostEncoder encoder;
    CHECK(portable_time_trial_ghost_encoder_init(&encoder,record+PORTABLE_TIME_TRIAL_GHOST_PAYLOAD_OFFSET,PORTABLE_TIME_TRIAL_GHOST_PAYLOAD_SIZE));
    for(unsigned tick=0;tick<280;++tick)CHECK(portable_time_trial_ghost_encoder_append(&encoder,0x8000));
    portable_time_trial_ghost_record_set_time(record,100);
    CHECK(portable_time_trial_ghost_store_save(local,&identity,record,stored)==PORTABLE_TIME_TRIAL_GHOST_STORE_OK);
    CHECK(portable_time_trial_ghost_export(&pack,directory,identity.course_id)==PORTABLE_TIME_TRIAL_GHOST_STORE_OK);
    PortableGhostExchangeInfo info=portable_time_trial_ghost_exchange_inspect(&pack,incoming,stored);
    CHECK(info.status==PORTABLE_TIME_TRIAL_GHOST_STORE_OK&&info.ticks==100&&memcmp(record,stored,sizeof record)==0);
    CHECK(!portable_time_trial_ghost_received_path(slots[0],directory,0));
    CHECK(!portable_time_trial_ghost_received_path(slots[0],directory,3));
    for(unsigned slot=1;slot<=2;++slot){
        CHECK(portable_time_trial_ghost_received_path(slots[slot-1],directory,slot));
        CHECK(portable_time_trial_ghost_receive(&pack,incoming,directory,slot)==PORTABLE_TIME_TRIAL_GHOST_STORE_OK);
    }
    PortableGhostLibraryInventory inventory;
    CHECK(portable_time_trial_ghost_library_query(directory,identity.course_id,&inventory)==PORTABLE_GHOST_LIBRARY_REUSE&&inventory.used==1);
    /* Content mismatch and structurally corrupt RLE must leave both slots intact. */
    identity.deterministic_digest[0]^=1;
    CHECK(portable_time_trial_ghost_store_replace(incoming,&identity,record,stored)==PORTABLE_TIME_TRIAL_GHOST_STORE_OK);
    CHECK(portable_time_trial_ghost_receive(&pack,incoming,directory,1)==PORTABLE_TIME_TRIAL_GHOST_STORE_INCOMPATIBLE);
    identity.deterministic_digest[0]^=1;
    memset(record+PORTABLE_TIME_TRIAL_GHOST_PAYLOAD_OFFSET,1,PORTABLE_TIME_TRIAL_GHOST_PAYLOAD_SIZE);
    CHECK(portable_time_trial_ghost_store_replace(incoming,&identity,record,stored)==PORTABLE_TIME_TRIAL_GHOST_STORE_OK);
    CHECK(portable_time_trial_ghost_receive(&pack,incoming,directory,2)==PORTABLE_TIME_TRIAL_GHOST_STORE_INVALID);
    for(unsigned slot=0;slot<2;++slot){
        info=portable_time_trial_ghost_exchange_inspect(&pack,slots[slot],stored);
        CHECK(info.status==PORTABLE_TIME_TRIAL_GHOST_STORE_OK&&info.ticks==100);
    }
    /* An explicit receive may replace an invalid destination, without touching its neighbor. */
    CHECK(portable_time_trial_ghost_store_load(local,&identity,record)==PORTABLE_TIME_TRIAL_GHOST_STORE_OK);
    portable_time_trial_ghost_record_set_time(record,200);
    CHECK(portable_time_trial_ghost_store_replace(incoming,&identity,record,stored)==PORTABLE_TIME_TRIAL_GHOST_STORE_OK);
    file=fopen(slots[0],"wb");CHECK(file&&fputs("bad",file)>=0&&fclose(file)==0);
    CHECK(portable_time_trial_ghost_receive(&pack,incoming,directory,1)==PORTABLE_TIME_TRIAL_GHOST_STORE_OK);
    CHECK(portable_time_trial_ghost_exchange_inspect(&pack,slots[0],stored).ticks==200);
    CHECK(portable_time_trial_ghost_exchange_inspect(&pack,slots[1],stored).ticks==100);
    CHECK(portable_time_trial_ghost_received_erase(directory,1)&&test_path_exists(slots[1])&&test_path_exists(local));
    CHECK(portable_time_trial_ghost_received_erase(directory,1));
    CHECK(portable_time_trial_ghost_received_erase(directory,2));
    CHECK(remove(local)==0&&remove(incoming)==0&&test_rmdir(directory)==0);
    free(storage);free(bytes);puts("portable ghost exchange: PASS");return 0;
}
