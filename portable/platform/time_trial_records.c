#include "time_trial_records.h"
#include "mod_identity.h"
#include "time_trial_results.h"

#include "content_registry.h"
#include "course_mirror.h"
#include "frontend_image_renderer.h"
#include "character_menu_renderer.h"
#include "course_menu_renderer.h"
#include "menu_font.h"
#include "menu_chrome_renderer.h"
#include "native_image.h"
#include "frame_scheduler.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int record_path(char path[4096],const char *directory,uint32_t course,
    const char *extension)
{
    if(directory==NULL||directory[0]=='\0'||course==0u)return 0;
    const size_t length=strlen(directory);
    const char last=directory[length-1u];
    /* Local records and ghosts coexist per content set; the ghost header also
     * checks compatibility when a file is exchanged or moved. */
    const int written=snprintf(path,4096,"%s%scourse-%u-solo%s.%s",directory,
        last=='/'||last=='\\'?"":"/",course,
        portable_mod_identity_record_suffix(),extension);
    return written>0&&written<4096;
}

int portable_time_trial_records_load(const PortableAssetPackView *pack,
    uint32_t course_id,uint32_t character_id,const char *directory,
    PortableTimeTrialRecords *out)
{
    char best[4096],ghost[4096];
    if(out==NULL||!record_path(best,directory,course_id,"pb")||
       !record_path(ghost,directory,course_id,"ghost"))return 0;
    PortableContentSelection selection;
    PortableRecoveredRaceView race;
    PortableRecoveredPackStorage *storage=malloc(sizeof *storage);
    if(storage==NULL)return 0;
    const int valid=portable_content_registry_select(pack,course_id,character_id,&selection)==
        PORTABLE_CONTENT_OK&&portable_course_open_race(pack,&selection,storage,&race,portable_mod_identity_mirror())==
        PORTABLE_RECOVERED_PACK_OK;
    if(!valid){free(storage);return 0;}
    PortableTimeTrialRecords next={.course_id=course_id};
    (void)portable_time_trial_ghost_library_query(directory,course_id,&next.local_ghosts);
    PortableAssetSectionView ghost_rules;
    const PortableAssetPackStatus ghost_rules_status=portable_asset_pack_find_section(pack,94u,1u,&ghost_rules);
    if(ghost_rules_status==PORTABLE_ASSET_PACK_OK){
        if(!portable_records_ghost_rules_open(&ghost_rules,&next.ghost_rules)){free(storage);return 0;}
        next.ghost_rules_ready=1u;
    }
    next.status=portable_personal_best_load_table(best,&race.rules.compatibility,&next.table);
    if(race.time_trial_record_defaults!=NULL){
        PortableGpResultsRecord actual[6];
        for(unsigned row=0u;row<6u;++row){
            const PortablePersonalBestRecord *value=row<5u?&next.table.races[row]:&next.table.best_lap;
            const uint64_t cs=(uint64_t)value->ticks*PORTABLE_FRAME_TICK_NUMERATOR*100u/PORTABLE_FRAME_TICK_DENOMINATOR;
            actual[row]=(PortableGpResultsRecord){.flags=value->ticks?value->character_class:0x10u,
                .time_centiseconds=(uint16_t)(cs>59999u?59999u:cs),.roster_driver_id=value->roster_driver_id};
        }
        portable_time_trial_results_merge(race.time_trial_record_defaults,actual,next.displayed_records);
        next.displayed_records_ready=1u;
    }
    uint8_t *record=malloc(PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE);
    if(record==NULL){free(storage);return 0;}
    next.ghost_status=portable_time_trial_ghost_store_load(ghost,&race.rules.compatibility,record);
    if(next.ghost_status==PORTABLE_TIME_TRIAL_GHOST_STORE_INCOMPATIBLE){
        char reason[2200];
        next.ghost_mods_differ=(uint8_t)portable_time_trial_ghost_store_describe_refusal(ghost,reason,sizeof reason);
    }
    if(next.ghost_status==PORTABLE_TIME_TRIAL_GHOST_STORE_OK){
        next.ghost.ticks=portable_time_trial_ghost_record_time(record);
        next.ghost.character_class=portable_time_trial_ghost_record_driver(record);
        next.ghost.roster_driver_id=portable_time_trial_ghost_record_roster_driver(record);
    }
    for(unsigned slot=1u;slot<=PORTABLE_TIME_TRIAL_RECEIVED_GHOST_SLOTS;++slot){
        char path[4096];
        if(!portable_time_trial_ghost_received_path(path,directory,slot)){free(record);free(storage);return 0;}
        next.received[slot-1u]=portable_time_trial_ghost_exchange_inspect(pack,path,record);
        /* A retained invalid or incompatible file still occupies its slot. */
        if(next.received[slot-1u].status!=PORTABLE_TIME_TRIAL_GHOST_STORE_NOT_FOUND)
            ++next.received_used;
    }
    free(record);free(storage);*out=next;return 1;
}

int portable_time_trial_records_erase_ghost(const char *directory,uint32_t course_id)
{
    char ghost[4096];
    if(!record_path(ghost,directory,course_id,"ghost"))return 0;
    /* 805D12C clears ghost presence independently of recorded times. */
    return remove(ghost)==0||errno==ENOENT;
}

int portable_time_trial_records_erase(const char *directory,uint32_t course_id)
{
    char best[4096],ghost[4096];
    if(!record_path(best,directory,course_id,"pb")||
       !record_path(ghost,directory,course_id,"ghost"))return 0;
    int ok=1;
    if(remove(best)!=0&&errno!=ENOENT)ok=0;
    if(remove(ghost)!=0&&errno!=ENOENT)ok=0;
    return ok;
}

static PortableFrontendPresentationStatus label(const PortableAssetPackView *pack,
    uint32_t id,PortableSoftwareFrame *frame,int x,int y,unsigned scale)
{
    PortableAssetSectionView section;
    PortableNativeImage image;
    if(portable_asset_pack_find_section(pack,PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,
       id,&section)!=PORTABLE_ASSET_PACK_OK)return PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION;
    if(!portable_native_image_open(section.bytes,section.size,&image))
        return PORTABLE_FRONTEND_PRESENTATION_BAD_SECTION;
    unsigned left=image.width,top=image.height,right=0u,bottom=0u;
    for(unsigned row=0u;row<image.height;++row)
    for(unsigned column=0u;column<image.width;++column)
        if((portable_native_image_pixel(&image,column,row)>>24)==255u){
            if(column<left)left=column;
            if(row<top)top=row;
            if(column>right)right=column;
            if(row>bottom)bottom=row;
        }
    if(left==image.width)return PORTABLE_FRONTEND_PRESENTATION_BAD_SECTION;
    const unsigned max_height=scale==256u?16u:10u;
    const unsigned max_width=scale==256u?190u:80u;
    if((bottom-top+1u)*scale>max_height*256u)scale=max_height*256u/(bottom-top+1u);
    if((right-left+1u)*scale>max_width*256u)scale=max_width*256u/(right-left+1u);
    return portable_frontend_image_draw_scaled(pack,id,frame,
        x-image.anchor_x*(int)scale/256-(int)(left*scale/256u),
        y-image.anchor_y*(int)scale/256-(int)(top*scale/256u),
        0,0,240,160,scale);
}

/* One 0800C6D4 glyph cell: 0-9, 10 ', 11 ", 12-13 the dash halves. */
static void glyph(const PortableAssetPackView *pack,PortableSoftwareFrame *frame,unsigned cell,int x,int y)
{
    (void)portable_frontend_image_draw(pack,PORTABLE_RECORDS_GLYPHS,frame,x-8*(int)cell,y,x,y,x+8,y+16);
}

/* M'SS"CC in the 0800C6D4 glyphs, 56 px wide. */
static void time_centiseconds_glyphs(const PortableAssetPackView *pack,PortableSoftwareFrame *frame,
    uint32_t cs,int empty,int x,int y)
{
    if(empty){
        static const uint8_t cells[7]={12u,10u,12u,13u,11u,12u,13u};
        for(unsigned i=0u;i<7u;++i)glyph(pack,frame,cells[i],x+8*(int)i,y);
        return;
    }
    const unsigned minutes=cs/6000u>9u?9u:cs/6000u;
    const unsigned cells[7]={minutes,10u,cs/1000u%6u,cs/100u%10u,11u,cs/10u%10u,cs%10u};
    for(unsigned i=0u;i<7u;++i)glyph(pack,frame,cells[i],x+8*(int)i,y);
}
static void time_glyphs(const PortableAssetPackView *pack,PortableSoftwareFrame *frame,
    uint32_t ticks,int x,int y)
{
    const uint64_t cs=(uint64_t)ticks*PORTABLE_FRAME_TICK_NUMERATOR*100u/PORTABLE_FRAME_TICK_DENOMINATOR;
    time_centiseconds_glyphs(pack,frame,(uint32_t)(cs>59999u?59999u:cs),ticks==0u,x,y);
}

/* 0800C6D4 row: rank (or blank for the lap), dash, then the time. */
static void time_row(const PortableAssetPackView *pack,PortableSoftwareFrame *frame,
    unsigned rank,uint32_t ticks,int y)
{
    if(rank!=0u)glyph(pack,frame,rank,8,y);
    glyph(pack,frame,12u,16,y);glyph(pack,frame,13u,24,y);
    time_glyphs(pack,frame,ticks,32,y);
}

/* A ghost's driver name and time. */
static PortableFrontendPresentationStatus result_row(const PortableAssetPackView *pack,
    const PortableFrontendFlow *flow,const PortablePersonalBestRecord *record,
    PortableSoftwareFrame *frame,int y)
{
    time_glyphs(pack,frame,record->ticks,170,y);
    if(record->ticks==0u)return PORTABLE_FRONTEND_PRESENTATION_OK;
    PortableCharacterContent character;
    if(portable_content_registry_saved_driver(pack,record->character_class,
           record->roster_driver_id,&character)!=PORTABLE_CONTENT_OK)
        return PORTABLE_FRONTEND_PRESENTATION_BAD_SECTION;
    const uint32_t name=portable_character_menu_name_id(pack,flow,character.id);
    if(name==0u)return PORTABLE_FRONTEND_PRESENTATION_BAD_SECTION;
    return label(pack,name,frame,36,y+3,192u);
}

static const char *ghost_state_text(PortableTimeTrialGhostStoreStatus status,uint8_t mods_differ)
{
    return status==PORTABLE_TIME_TRIAL_GHOST_STORE_OK?"GHOST: SAVED":
        status==PORTABLE_TIME_TRIAL_GHOST_STORE_NOT_FOUND?"GHOST: NONE":
        status==PORTABLE_TIME_TRIAL_GHOST_STORE_INCOMPATIBLE?(mods_differ?"GHOST: NEEDS OTHER MODS":"GHOST: DIFFERENT CONTENT"):
        status==PORTABLE_TIME_TRIAL_GHOST_STORE_INVALID?"GHOST: INVALID FILE":"GHOST: READ FAILED";
}

/* The ghost slots (phases 4-6) are native-only and use the 58B0 window. */
static PortableFrontendPresentationStatus ghost_board(const PortableAssetPackView *pack,
    const PortableFrontendFlow *flow,const PortableTimeTrialRecords *records,PortableSoftwareFrame *frame)
{
    const unsigned slot=flow->record_ghost_slot;
    const PortableGhostExchangeInfo *received=slot!=0u?&records->received[slot-1u]:NULL;
    PortablePersonalBestRecord ghost=records->ghost;
    PortableTimeTrialGhostStoreStatus ghost_status=records->ghost_status;
    uint8_t mods_differ=records->ghost_mods_differ;
    if(received!=NULL){
        ghost=(PortablePersonalBestRecord){.ticks=received->ticks,.character_class=received->identity.character_class,
            .roster_driver_id=received->identity.roster_driver_id};
        ghost_status=received->status;mods_differ=received->mods_differ;
    }
    PortableFrontendPresentationStatus status=portable_menu_panel(pack,frame,0,0,240u,160u);
    if(status!=PORTABLE_FRONTEND_PRESENTATION_OK)return status;
    portable_menu_text(pack,frame,16,12,slot==0u?"< LOCAL GHOST >":slot==1u?"< RECEIVED 1 >":"< RECEIVED 2 >");
    status=result_row(pack,flow,&ghost,frame,40);
    if(status!=PORTABLE_FRONTEND_PRESENTATION_OK)return status;
    portable_menu_text_wrapped(pack,frame,16,68,26u,14,2u,
        slot==0u?"A: EXPORT COPY TO SAVE FOLDER":"A: RECEIVE FILE  SELECT: ERASE");
    portable_menu_text_wrapped(pack,frame,12,104,27u,14,1u,records->notice!=NULL?records->notice:
        records->erase_failed?"ERASE FAILED - TRY AGAIN":ghost_state_text(ghost_status,mods_differ));
    portable_menu_text(pack,frame,12,124,"LEFT/RIGHT: SLOT  B: BACK");
    return status;
}

static uint32_t listed_course_id(const PortableFrontendFlow *flow,unsigned index)
{
    for(size_t i=0u;i<flow->config.course_count;++i){
        const PortableFrontendCourseSlot *slot=&flow->config.courses[i];
        if(slot->available&&slot->cup==index/4u&&slot->track==index%4u)return slot->content_id;
    }
    return 0u;
}

static PortableFrontendPresentationStatus ghost_presence(const PortableAssetPackView *pack,
    const PortableFrontendFlow *flow,const PortableTimeTrialRecords *records,PortableSoftwareFrame *frame)
{
    if(!records->ghost_rules_ready)return PORTABLE_FRONTEND_PRESENTATION_OK;
    const PortableRecordsGhostRules *rules=&records->ghost_rules;
    const unsigned local=records->local_ghosts.used>10u?10u:records->local_ghosts.used;
    const unsigned received=records->received_used>2u?2u:records->received_used;
    const uint32_t header[3]={rules->header_base,rules->local_counts[local],rules->received_counts[received]};
    for(unsigned i=0u;i<3u;++i){
        const PortableFrontendPresentationStatus status=portable_frontend_image_draw(pack,header[i],frame,
            216,11,0,0,240,160);
        if(status!=PORTABLE_FRONTEND_PRESENTATION_OK)return status;
    }
    uint8_t entries[PORTABLE_FRONTEND_RECORDS_MAX];
    const unsigned count=portable_frontend_flow_records_list(flow,entries);
    for(unsigned row=0u;row<PORTABLE_FRONTEND_RECORDS_ROWS;++row){
        const unsigned index=(unsigned)flow->records_top+row;
        if(index>=count)break;
        const uint32_t course=listed_course_id(flow,entries[index]);
        if(!course)continue;
        const unsigned selected=course==flow->selected_course;
        unsigned present=0u;
        for(unsigned slot=0u;slot<PORTABLE_TIME_TRIAL_LOCAL_GHOST_SLOTS;++slot)
            present|=records->local_ghosts.courses[slot]==course;
        if(present){
            const uint32_t image=portable_records_ghost_image(&rules->groups[selected?0u:1u],flow->page_ticks);
            const PortableFrontendPresentationStatus status=portable_frontend_image_draw(pack,image,frame,
                208,24+16*(int)row,0,0,240,160);
            if(status!=PORTABLE_FRONTEND_PRESENTATION_OK)return status;
        }
        for(unsigned slot=0u;slot<PORTABLE_TIME_TRIAL_RECEIVED_GHOST_SLOTS;++slot){
            if(records->received[slot].status==PORTABLE_TIME_TRIAL_GHOST_STORE_NOT_FOUND||
               records->received[slot].identity.course_id!=course)continue;
            const uint32_t image=portable_records_ghost_image(&rules->groups[selected?4u:5u],flow->page_ticks);
            const PortableFrontendPresentationStatus status=portable_frontend_image_draw(pack,image,frame,
                224,24+16*(int)row,0,0,240,160);
            if(status!=PORTABLE_FRONTEND_PRESENTATION_OK)return status;
        }
    }
    return PORTABLE_FRONTEND_PRESENTATION_OK;
}

PortableFrontendPresentationStatus portable_time_trial_records_render(
    const PortableAssetPackView *pack,const PortableFrontendFlow *flow,
    const PortableTimeTrialRecords *records,PortableSoftwareFrame *frame)
{
    if(pack==NULL||flow==NULL||records==NULL||frame==NULL||
       flow->page!=PORTABLE_FRONTEND_PAGE_RECORDS||flow->course_phase<2u||
       records->course_id!=flow->selected_course||
       flow->record_ghost_slot>PORTABLE_TIME_TRIAL_RECEIVED_GHOST_SLOTS)
        return PORTABLE_FRONTEND_PRESENTATION_INVALID_ARGUMENT;
    const int ghost_view=flow->course_phase>=4u;
    PortableFrontendPresentationStatus status;
    if(ghost_view)status=ghost_board(pack,flow,records,frame);
    else{
        status=ghost_presence(pack,flow,records,frame);
        if(status!=PORTABLE_FRONTEND_PRESENTATION_OK)return status;
        /* 0800C6D4 board on the list's left: five ranks, then the best lap. */
        status=portable_frontend_image_draw(pack,PORTABLE_RECORDS_BEST,frame,8,8,0,0,240,160);
        if(status==PORTABLE_FRONTEND_PRESENTATION_OK)
            status=portable_frontend_image_draw(pack,PORTABLE_RECORDS_LAP,frame,8,104,0,0,240,160);
        if(status!=PORTABLE_FRONTEND_PRESENTATION_OK)return status;
        if(records->displayed_records_ready){
            for(unsigned row=0u;row<6u;++row){
                const int y=row<5u?24+16*(int)row:120;
                if(row<5u)glyph(pack,frame,row+1u,8,y);
                glyph(pack,frame,12u,16,y);glyph(pack,frame,13u,24,y);
                time_centiseconds_glyphs(pack,frame,records->displayed_records[row].time_centiseconds,0,32,y);
            }
        }else{
            for(unsigned row=0u;row<PORTABLE_PERSONAL_BEST_COUNT;++row)
                time_row(pack,frame,row+1u,records->table.races[row].ticks,24+16*(int)row);
            time_row(pack,frame,0u,records->table.best_lap.ticks,120);
        }
        /* Native-only states the original never reaches get a notice. */
        const char *notice=records->notice!=NULL?records->notice:
            records->erase_failed?"ERASE FAILED - TRY AGAIN":
            records->status==PORTABLE_PERSONAL_BEST_INCOMPATIBLE?"RECORDS: DIFFERENT CONTENT":
            records->status==PORTABLE_PERSONAL_BEST_INVALID?"RECORDS: INVALID FILE":
            records->status==PORTABLE_PERSONAL_BEST_IO_ERROR?"RECORDS: READ FAILED":NULL;
        if(notice!=NULL){
            status=portable_menu_panel(pack,frame,0,128,240u,32u);
            if(status!=PORTABLE_FRONTEND_PRESENTATION_OK)return status;
            portable_menu_text_wrapped(pack,frame,12,136,27u,14,1u,notice);
        }
    }
    if(status!=PORTABLE_FRONTEND_PRESENTATION_OK)return status;
    if(flow->course_phase==3u||flow->course_phase==5u||flow->course_phase==6u){
        status=portable_menu_panel(pack,frame,16,40,208u,72u);
        if(status!=PORTABLE_FRONTEND_PRESENTATION_OK)return status;
        portable_menu_text_wrapped(pack,frame,28,48,23u,14,2u,
            flow->course_phase==6u?"RECEIVE FILE? REPLACE THIS SLOT?":ghost_view?"ERASE THIS GHOST? TIMES ARE KEPT.":
            "THIS COURSE: ERASE TIMES AND GHOST?");
        portable_menu_text(pack,frame,64,82,"NO");
        portable_menu_text(pack,frame,152,82,"YES");
        status=portable_frontend_image_draw(pack,PORTABLE_MENU_OPTION_CURSOR,frame,
            flow->record_erase_choice?146:58,90,0,0,240,160);
    }
    return status;
}

uint8_t portable_time_trial_records_ghost_choices(const PortableTimeTrialRecords *records)
{
    if(records==NULL||records->course_id==0u)return 0u;
    uint8_t mask=records->ghost_status==PORTABLE_TIME_TRIAL_GHOST_STORE_OK?1u:0u;
    for(unsigned index=0u;index<PORTABLE_TIME_TRIAL_RECEIVED_GHOST_SLOTS;++index)
        if(records->received[index].status==PORTABLE_TIME_TRIAL_GHOST_STORE_OK&&
           records->received[index].identity.course_id==records->course_id)
            mask|=(uint8_t)(1u<<(index+1u));
    return mask;
}

PortableFrontendPresentationStatus portable_time_trial_ghost_choice_render(
    const PortableAssetPackView *pack,const PortableFrontendFlow *flow,
    const PortableTimeTrialRecords *records,PortableSoftwareFrame *frame)
{
    if(pack==NULL||flow==NULL||records==NULL||frame==NULL||flow->ghost_choice>2u||
       flow->ghost_choices==0u||records->course_id!=flow->selected_course)
        return PORTABLE_FRONTEND_PRESENTATION_INVALID_ARGUMENT;
    PortablePersonalBestRecord ghost=records->ghost;
    if(flow->ghost_choice!=0u){
        const PortableGhostExchangeInfo *received=&records->received[flow->ghost_choice-1u];
        ghost=(PortablePersonalBestRecord){.ticks=received->ticks,.character_class=received->identity.character_class,
            .roster_driver_id=received->identity.roster_driver_id};
    }
    PortableFrontendPresentationStatus status=portable_menu_panel(pack,frame,8,40,224u,96u);
    if(status!=PORTABLE_FRONTEND_PRESENTATION_OK)return status;
    portable_menu_text(pack,frame,20,46,"RACE THIS GHOST");
    portable_menu_text(pack,frame,20,62,flow->ghost_choice==0u?"LOCAL":
        flow->ghost_choice==1u?"RECEIVED 1":"RECEIVED 2");
    status=result_row(pack,flow,&ghost,frame,78);
    portable_menu_text_wrapped(pack,frame,20,96,25u,14,2u,"UP/DOWN: GHOST  A: RACE  B: BACK");
    return status;
}
