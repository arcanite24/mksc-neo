#include "content_import.h"
#include "text_number.h"

#include <stdint.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>


#include "driver_pilot.h"
#include "track_asset.h"
#include "track_geometry.h"
#include "source_course_metadata.h"
#include "track_raster.h"
#include "terrain_asset.h"
#include "sky_asset.h"
#include "character_asset.h"
#include "track_tileset_asset.h"
#include "road_palette_asset.h"
#include "source_motion_tables.h"
#include "source_normal_response_table.h"
#include "source_b550_tables.h"
#include "source_vector_tables.h"
#include "track_object_asset.h"
#include "track_object_decoder.h"
#include "track_runtime_map.h"
#include "road_map_asset.h"
#include "minimap_asset.h"
#include "hud_asset.h"
#include "item_box_asset.h"
#include "race_runtime.h"

static int export_open(FILE **file, const char *directory, const char *name)
{
    char path[1024];
    const int length = snprintf(path, sizeof path, "%s/%s", directory, name);
    if (length < 0 || (size_t)length >= sizeof path) return 0;
    *file = fopen(path, "wb");
    return *file != NULL;
}

static int export_u16(FILE *file, uint16_t value)
{
    const uint8_t bytes[2]={(uint8_t)value,(uint8_t)(value>>8)};
    return fwrite(bytes,1,sizeof bytes,file)==sizeof bytes;
}

static int export_u32(FILE *file, uint32_t value)
{
    const uint8_t bytes[4]={(uint8_t)value,(uint8_t)(value>>8),
        (uint8_t)(value>>16),(uint8_t)(value>>24)};
    return fwrite(bytes,1,sizeof bytes,file)==sizeof bytes;
}

static uint32_t export_read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u) |
           ((uint32_t)bytes[2] << 16u) | ((uint32_t)bytes[3] << 24u);
}

static int export_i16_array(FILE *file,const int16_t *values,size_t count)
{
    for(size_t i=0;i<count;++i)if(!export_u16(file,(uint16_t)values[i]))return 0;
    return 1;
}

static int export_i32_array(FILE *file,const int32_t *values,size_t count)
{
    for(size_t i=0;i<count;++i)if(!export_u32(file,(uint32_t)values[i]))return 0;
    return 1;
}

typedef struct
{
    uint32_t course_id;
    uint32_t course_revision;
    uint32_t rules_id;
    uint32_t rules_revision;
    uint32_t state_schema_version;
    int32_t start_x_q16;
    int32_t start_y_q16;
    uint16_t start_route_index;
    uint8_t start_checkpoint_raw;
    PortableRaceProgressKind progress_kind;
    PortableFinishGateKind finish_kind;
    int32_t finish_y_q16;
    uint32_t expected_recovered_ticks;
    uint32_t expected_recovered_race_ticks;
    uint32_t expected_demo_ticks;
    uint32_t expected_demo_left_ticks;
    uint32_t expected_demo_right_ticks;
} RecoveredExportSpec;

typedef struct
{
    uint32_t header_index;
    uint32_t expected_recovered_ticks;
    uint32_t expected_recovered_race_ticks;
    uint32_t expected_demo_ticks;
    uint32_t expected_demo_left_ticks;
    uint32_t expected_demo_right_ticks;
} CourseExportAcceptance;

/* Normalize a decimal token before using the bounded integer parser. Long
 * zero prefixes and whitespace remain valid without an unbounded buffer. */
static int read_acceptance_u32(FILE *file, uint32_t *out)
{
    int byte;
    do { byte = fgetc(file); } while (byte == ' ' || byte == '\t' || byte == '\n' ||
        byte == '\r' || byte == '\v' || byte == '\f');
    if (byte == '+')
        byte = fgetc(file);
    if (byte < '0' || byte > '9')
        return 0;
    char token[10];
    size_t count = 0u;
    do
    {
        if (count != 0u || byte != '0')
        {
            if (count == sizeof token)
                return 0;
            token[count++] = (char)byte;
        }
        byte = fgetc(file);
    } while (byte >= '0' && byte <= '9');
    if (byte != EOF && ungetc(byte, file) == EOF)
        return 0;
    if (count == 0u)
        token[count++] = '0';
    const char *cursor = token;
    uint64_t value;
    if (!portable_text_u64(&cursor, token + count, 10u, UINT32_MAX, &value))
        return 0;
    *out = (uint32_t)value;
    return 1;
}

static int read_course_export_acceptance(const char *path,
                                         CourseExportAcceptance *out)
{
    FILE *file;
    if (path == NULL || out == NULL || (file = fopen(path, "r")) == NULL)
        return 0;
    uint32_t *fields[] = {&out->header_index, &out->expected_recovered_ticks,
        &out->expected_recovered_race_ticks, &out->expected_demo_ticks,
        &out->expected_demo_left_ticks, &out->expected_demo_right_ticks};
    int parsed = 1;
    for (size_t i = 0u; i < sizeof fields / sizeof fields[0]; ++i)
        if (!read_acceptance_u32(file, fields[i]))
        {
            parsed = 0;
            break;
        }
    int byte;
    do { byte = fgetc(file); } while (byte == ' ' || byte == '\t' || byte == '\n' ||
        byte == '\r' || byte == '\v' || byte == '\f');
    const int read_failed = ferror(file);
    const int close_failed = fclose(file) != 0;
    return parsed && byte == EOF && !read_failed && !close_failed &&
           out->expected_recovered_ticks != 0u && out->expected_recovered_race_ticks != 0u &&
           out->expected_demo_ticks != 0u;
}

static int export_recovered_sections(
    const char *directory,const PortableTrackGeometryView *geometry,
    const PortableDriverPilotDependencies *d,
    const PortableTrackObjectStream *item_box_stream,
    const PortableTrackObjectStream *coin_stream,uint8_t object_class,
    const PortableTrackTilesetView *tileset,const PortableSkyAssetView *sky,
    const PortableCharacterAssetView *character,
    const PortableMinimapAssetView *minimap,const PortableHudAssetView *hud,
    const RecoveredExportSpec *spec,const uint8_t *rom,size_t rom_size)
{
    FILE *f=NULL;
    int16_t class_local_tables[2][8][PORTABLE_SOURCE_LOCAL_TABLE_COUNT];
    int16_t steering_response_slots[
        PORTABLE_SOURCE_STEERING_RESPONSE_SLOT_COUNT *
        PORTABLE_SOURCE_STEERING_RESPONSE_COUNT];
    int ok=1;

    if(directory==NULL||geometry==NULL||d==NULL||item_box_stream==NULL||
       coin_stream==NULL||
       tileset==NULL||sky==NULL||
       character==NULL||minimap==NULL||hud==NULL||spec==NULL||rom==NULL||
       rom_size<0xeda00u)return 1;
    if(!export_open(&f,directory,"course-manifest.bin"))return 1;
    {const uint32_t fields[17]={UINT32_C(0x31535243),spec->course_id,
        spec->course_revision,
        spec->course_id,spec->course_id,spec->course_id,spec->course_id,
        spec->course_id,spec->course_id,spec->course_id,1,1,
        spec->expected_recovered_ticks,spec->expected_recovered_race_ticks,
        spec->expected_demo_ticks,spec->expected_demo_left_ticks,
        spec->expected_demo_right_ticks};
     for(size_t i=0;ok&&i<17;++i)ok=export_u32(f,fields[i]);}
    ok=ok&&fclose(f)==0;if(!ok)return 1;

    if(!export_open(&f,directory,"item-box.bin"))return 1;
    ok=export_u32(f,UINT32_C(0x31584249))&&
       export_u32(f,PORTABLE_ITEM_BOX_TILE_BYTES)&&
       export_u32(f,PORTABLE_ITEM_BOX_PALETTE_COUNT)&&
       export_u32(f,PORTABLE_ITEM_BOX_FRAME_COUNT)&&
       export_u32(f,32u)&&export_u32(f,32u);
    for(size_t frame_index=0u;ok&&
        frame_index<PORTABLE_ITEM_BOX_FRAME_COUNT;++frame_index){
        const uint32_t address=(uint32_t)rom[0xed9acu+frame_index*4u]|
            ((uint32_t)rom[0xed9adu+frame_index*4u]<<8u)|
            ((uint32_t)rom[0xed9aeu+frame_index*4u]<<16u)|
            ((uint32_t)rom[0xed9afu+frame_index*4u]<<24u);
        const uint32_t offset=address&UINT32_C(0x01ffffff);
        ok=offset<=rom_size&&PORTABLE_ITEM_BOX_FRAME_BYTES<=rom_size-offset&&
           fwrite(rom+offset,1,PORTABLE_ITEM_BOX_FRAME_BYTES,f)==
               PORTABLE_ITEM_BOX_FRAME_BYTES;
    }
    for(size_t index=0u;ok&&index<PORTABLE_ITEM_BOX_PALETTE_COUNT;++index)
        ok=export_u16(f,(uint16_t)rom[0x6d71cu+index*2u]|
            (uint16_t)((uint16_t)rom[0x6d71du+index*2u]<<8u));
    ok=ok&&fclose(f)==0;if(!ok)return 1;

    if(!export_open(&f,directory,"objects.bin"))return 1;
    {
        uint32_t item_box_count=0u;
        for(uint32_t index=0u;index<item_box_stream->record_count;++index){
            PortableTrackRawObject object;
            PortableTrackObjectDecode decoded={0};
            const int read_ok=portable_track_raw_object_read(
                item_box_stream,index,&object)==PORTABLE_TRACK_OBJECT_ASSET_OK;
            const int decode_status=read_ok?portable_track_object_decode(
                rom,rom_size,object_class,object.raw_type,&decoded):
                PORTABLE_TRACK_OBJECT_DECODER_INVALID_ARGUMENT;
            if(!read_ok||(decode_status!=PORTABLE_TRACK_OBJECT_DECODER_OK&&
               decode_status!=PORTABLE_TRACK_OBJECT_DECODER_UNSUPPORTED_OBJECT)){
                ok=0;break;
            }
            if(decoded.source_code==UINT32_C(0xffff0000))++item_box_count;
        }
        ok=ok&&export_u32(f,UINT32_C(0x314a424f))&&
           export_u32(f,item_box_count);
        for(uint32_t index=0u;ok&&index<item_box_stream->record_count;++index){
            PortableTrackRawObject object;
            PortableTrackObjectDecode decoded={0};
            if(portable_track_raw_object_read(item_box_stream,index,&object)!=
                   PORTABLE_TRACK_OBJECT_ASSET_OK||
               (portable_track_object_decode(rom,rom_size,object_class,
                   object.raw_type,&decoded)!=PORTABLE_TRACK_OBJECT_DECODER_OK&&
                decoded.source_code!=UINT32_C(0xffff0000))){ok=0;break;}
            if(decoded.source_code!=UINT32_C(0xffff0000))continue;
            ok=export_u32(f,(uint32_t)object.x<<19u)&&
               export_u32(f,(uint32_t)object.y<<19u)&&
               export_u16(f,UINT16_C(0x8800))&&
               fputc(object.parameter,f)!=EOF&&fputc(0x1e,f)!=EOF;
        }
    }
    ok=ok&&fclose(f)==0;if(!ok)return 1;

    if(!export_open(&f,directory,"coins.bin"))return 1;
    ok=export_u32(f,UINT32_C(0x314e4943))&&
       export_u32(f,coin_stream->record_count);
    for(uint32_t index=0u;ok&&index<coin_stream->record_count;++index){
        uint8_t record[PORTABLE_TRACK_STREAM_RECORD_SIZE];
        ok=portable_track_object_stream_read(coin_stream,index,record)==
               PORTABLE_TRACK_OBJECT_ASSET_OK&&record[0]==UINT8_C(0x80)&&
           fwrite(record,1,sizeof record,f)==sizeof record;
    }
    ok=ok&&fclose(f)==0;if(!ok)return 1;

    if(!export_open(&f,directory,"charged-exit.bin"))return 1;
    {static const uint32_t offsets[5]={0x7ae50u,0x7aeecu,0x7ae58u,
                                      0x7ae60u,0x7aea0u};
     static const uint16_t sizes[5]={8u,8u,8u,62u,74u};
     static const uint16_t durations[5]={2u,3u,4u,4u,2u};
     uint32_t cell_offset=0u;
     ok=export_u32(f,UINT32_C(0x31435846))&&export_u32(f,0x2000u)&&
        export_u32(f,80u)&&export_u32(f,5u)&&export_u32(f,0x100u)&&
        export_u32(f,3u)&&export_u32(f,160u)&&export_u32(f,0u);
     for(size_t i=0;ok&&i<5u;++i){ok=export_u32(f,cell_offset)&&
        export_u16(f,sizes[i])&&export_u16(f,durations[i]);cell_offset+=sizes[i];}
     ok=ok&&fwrite(rom+0x66f0cu,1,0x2000u,f)==0x2000u;
     for(size_t i=0;ok&&i<80u;++i)ok=export_u16(f,
        (uint16_t)rom[0x66e4cu+i*2u]|
        (uint16_t)((uint16_t)rom[0x66e4cu+i*2u+1u]<<8));
     for(size_t i=0;ok&&i<5u;++i)
        ok=fwrite(rom+offsets[i],1,sizes[i],f)==sizes[i];}
    ok=ok&&fclose(f)==0;if(!ok)return 1;
    if(!export_open(&f,directory,"character-manifest.bin"))return 1;
    {const uint32_t fields[6]={UINT32_C(0x31524843),1,1,1,1,3};
     for(size_t i=0;ok&&i<6;++i)ok=export_u32(f,fields[i]);}
    ok=ok&&fclose(f)==0;if(!ok)return 1;
    if(!export_open(&f,directory,"rules.bin"))return 1;
    ok=export_u32(f,UINT32_C(0x314c5552))&&export_u32(f,spec->course_id)&&
       export_u32(f,spec->course_revision)&&export_u32(f,spec->rules_id)&&
       export_u32(f,spec->rules_revision)&&export_u32(f,2)&&export_u32(f,2)&&
       export_u32(f,3)&&export_u32(f,spec->state_schema_version)&&
       export_u32(f,(uint32_t)spec->start_x_q16)&&
       export_u32(f,(uint32_t)spec->start_y_q16)&&export_u16(f,0)&&
       export_u16(f,spec->start_route_index)&&
       export_u32(f,spec->progress_kind)&&
       export_u32(f,spec->finish_kind)&&
       export_u32(f,(uint32_t)spec->finish_y_q16)&&export_u16(f,3)&&export_u16(f,180)&&
       export_u16(f,180)&&export_u16(f,spec->start_checkpoint_raw);
    ok=ok&&fclose(f)==0;if(!ok)return 1;

    {
        const int gp_rows = spec->course_id == 24u;
        const uint32_t rows = gp_rows ? 3u : 1u;
        const uint32_t table_count = gp_rows ? 40u : 0u;
        const uint8_t *target_table = NULL;
        if (gp_rows)
        {
            const size_t def_slot = 0x0e7fecu + (size_t)object_class * 4u;
            uint32_t def_pointer, table_pointer;
            size_t def_offset, table_offset;
            if (def_slot > rom_size || rom_size - def_slot < 4u) return 1;
            def_pointer = export_read_u32(rom + def_slot);
            if (def_pointer < UINT32_C(0x08000000)) return 1;
            def_offset = (size_t)(def_pointer - UINT32_C(0x08000000));
            if (def_offset > rom_size || rom_size - def_offset < 0x20u) return 1;
            table_pointer = export_read_u32(rom + def_offset + 0x1cu);
            if (table_pointer < UINT32_C(0x08000000)) return 1;
            table_offset = (size_t)(table_pointer - UINT32_C(0x08000000));
            if (table_offset > rom_size || rom_size - table_offset < table_count)
                return 1;
            target_table = rom + table_offset;
            for (size_t i = 0u; i < table_count; ++i)
                if (target_table[i] >= rows) return 1;
        }
        if(!export_open(&f,directory,"route.bin"))return 1;
        ok=export_u32(f,UINT32_C(0x31545552))&&
           export_u32(f,geometry->nr_checkpoints);
        if (gp_rows) ok=ok&&export_u32(f,rows)&&export_u32(f,table_count);
        for(uint32_t row=0u;ok&&row<rows;++row)
        for(size_t i=0;ok&&i<geometry->nr_checkpoints;++i){PortableTrackCheckpoint c;
            ok=portable_track_geometry_read_checkpoint(geometry,(uint32_t)i,(uint8_t)row,&c)==0&&
               export_u32(f,(uint32_t)(c.x*8))&&export_u32(f,(uint32_t)(c.y*8))&&
               export_u32(f,c.ai_mode);}
        if (gp_rows) ok=ok&&fwrite(target_table,1,table_count,f)==table_count;
        ok=ok&&fclose(f)==0;if(!ok)return 1;
    }

    if(!export_open(&f,directory,"world.bin"))return 1;
    ok=export_u32(f,UINT32_C(0x31444c57))&&export_u32(f,(uint32_t)d->raster->width)&&
       export_u32(f,(uint32_t)d->raster->height)&&export_u32(f,(uint32_t)d->terrain->width)&&
       export_u32(f,(uint32_t)d->terrain->height)&&
       export_u32(f,(uint32_t)(d->raster->width*d->raster->height))&&
       export_u32(f,(uint32_t)(d->terrain->width*d->terrain->height))&&
       fwrite(d->raster->cells,1,d->raster->width*d->raster->height,f)==d->raster->width*d->raster->height&&
       fwrite(d->terrain->cells,1,d->terrain->width*d->terrain->height,f)==d->terrain->width*d->terrain->height;
    ok=ok&&fclose(f)==0;if(!ok)return 1;

    if(!export_open(&f,directory,"material.bin"))return 1;
    ok=export_u32(f,UINT32_C(0x3154414d))&&export_u32(f,256)&&
       fwrite(d->track_material->flags,1,256,f)==256;ok=ok&&fclose(f)==0;if(!ok)return 1;

    if(!export_open(&f,directory,"motion.bin"))return 1;
    ok=export_u32(f,UINT32_C(0x31544f4d));
    {const uint32_t counts[8]={1024,64,8,8,8,256,10,
        PORTABLE_SOURCE_STEERING_RESPONSE_SLOT_COUNT *
        PORTABLE_SOURCE_STEERING_RESPONSE_COUNT};
     for(size_t i=0;ok&&i<8;++i)ok=export_u32(f,counts[i]);}
    for(size_t profile=0u;ok&&profile<2u;++profile)
        for(size_t class_index=0u;ok&&class_index<8u;++class_index)
            ok=portable_source_local_table_build(
                   rom,rom_size,(uint8_t)class_index,
                   profile==0u?0u:2u,class_local_tables[profile][class_index],
                   PORTABLE_SOURCE_LOCAL_TABLE_COUNT)==
               PORTABLE_SOURCE_MOTION_TABLES_OK;
    /* The normalized character manifest above selects source class 3. The
     * source indexes four consecutive 8-byte rows with driver +0xb0, so a
     * playable course pack must carry all four rows rather than only the
     * neutral row exposed by the current dependency view. */
    for(size_t slot=0u;ok&&slot<PORTABLE_SOURCE_STEERING_RESPONSE_SLOT_COUNT;
        ++slot)
        ok=portable_source_steering_response_build(
               rom,rom_size,3u,(uint8_t)slot,
               steering_response_slots+
                   slot*PORTABLE_SOURCE_STEERING_RESPONSE_COUNT,
               PORTABLE_SOURCE_STEERING_RESPONSE_COUNT)==
           PORTABLE_SOURCE_MOTION_TABLES_OK;
    ok=ok&&export_i32_array(f,d->sine_table,1024)&&
       export_i16_array(f,d->auto_acceleration_table,64)&&export_i16_array(f,d->brake_table,8)&&
       export_i16_array(f,d->brake_accelerate_table,8)&&export_i16_array(f,d->forward_response_curve,8)&&
       export_i16_array(f,&class_local_tables[0][0][0],256)&&
       export_i16_array(f,d->steering_delta_table,10)&&
       export_i16_array(f,steering_response_slots,
           PORTABLE_SOURCE_STEERING_RESPONSE_SLOT_COUNT *
           PORTABLE_SOURCE_STEERING_RESPONSE_COUNT);
    ok=ok&&fclose(f)==0;if(!ok)return 1;

    if(!export_open(&f,directory,"collision.bin"))return 1;
    ok=export_u32(f,UINT32_C(0x314c4f43));
    {const uint32_t counts[7]={8,8,4,4,4,4,8};
     for(size_t i=0;ok&&i<7;++i)ok=export_u32(f,counts[i]);}
    ok=ok&&export_i32_array(f,d->terrain_effect_tables->x_offsets,8)&&
       export_i32_array(f,d->terrain_effect_tables->y_offsets,8)&&
       export_i16_array(f,d->vector_alternate_x_components,4)&&
       export_i16_array(f,d->vector_alternate_y_components,4)&&
       export_i16_array(f,d->normal_resultant_x_table,4)&&
       export_i16_array(f,d->normal_resultant_y_table,4)&&
       export_i32_array(f,d->normal_response_table->entries,8);ok=ok&&fclose(f)==0;if(!ok)return 1;

    if(!export_open(&f,directory,"road.bin"))return 1;
    ok=export_u32(f,UINT32_C(0x31444152))&&export_u32(f,(uint32_t)tileset->tile_bytes)&&
       export_u32(f,(uint32_t)tileset->palette_count)&&export_u32(f,tileset->flags)&&
       export_u32(f,tileset->block_count)&&export_u32(f,0)&&
       fwrite(tileset->tiles,1,tileset->tile_bytes,f)==tileset->tile_bytes;
    for(size_t i=0;ok&&i<tileset->palette_count;++i)ok=export_u16(f,tileset->palette[i]);
    ok=ok&&fclose(f)==0;if(!ok)return 1;

    if(!export_open(&f,directory,"sky.bin"))return 1;
    ok=export_u32(f,UINT32_C(0x31594b53))&&export_u32(f,(uint32_t)sky->tile_bytes)&&
       export_u32(f,(uint32_t)sky->palette_count)&&export_u32(f,(uint32_t)sky->front_map_bytes)&&
       export_u32(f,(uint32_t)sky->mid_map_bytes)&&export_u32(f,(uint32_t)sky->back_map_bytes)&&
       export_u32(f,(uint32_t)sky->front_width_tiles)&&export_u32(f,(uint32_t)sky->mid_width_tiles)&&
       export_u32(f,(uint32_t)sky->back_width_tiles)&&export_u32(f,(uint32_t)sky->map_height_tiles)&&
       export_u32(f,sky->size)&&export_u32(f,sky->compression_flags)&&export_u32(f,sky->descriptor)&&
       export_u32(f,sky->palette_hardware_base)&&export_u32(f,sky->background_index)&&export_u32(f,0)&&
       fwrite(sky->tiles,1,sky->tile_bytes,f)==sky->tile_bytes;
    for(size_t i=0;ok&&i<sky->palette_count;++i)ok=export_u16(f,sky->palette[i]);
    ok=ok&&fwrite(sky->front_map,1,sky->front_map_bytes,f)==sky->front_map_bytes&&
       fwrite(sky->mid_map,1,sky->mid_map_bytes,f)==sky->mid_map_bytes&&
       fwrite(sky->back_map,1,sky->back_map_bytes,f)==sky->back_map_bytes;
    ok=ok&&fclose(f)==0;if(!ok)return 1;

    if(!export_open(&f,directory,"character.bin"))return 1;
    ok=export_u32(f,UINT32_C(0x31524143))&&export_u32(f,(uint32_t)character->tile_bytes)&&
       export_u32(f,(uint32_t)character->palette_count)&&export_u32(f,(uint32_t)character->width)&&
       export_u32(f,(uint32_t)character->height)&&export_u32(f,character->character_id)&&
       export_u32(f,character->frame_slot)&&export_u32(f,0)&&
       fwrite(character->tiles,1,character->tile_bytes,f)==character->tile_bytes;
    for(size_t i=0;ok&&i<character->palette_count;++i)ok=export_u16(f,character->palette[i]);
    ok=ok&&fclose(f)==0;if(!ok)return 1;

    if(!export_open(&f,directory,"minimap.bin"))return 1;
    ok=export_u32(f,UINT32_C(0x314e494d))&&export_u32(f,(uint32_t)minimap->cell_count)&&
       export_u32(f,(uint32_t)minimap->width_tiles)&&export_u32(f,(uint32_t)minimap->height_tiles)&&
       export_u32(f,(uint32_t)minimap->tile_bytes)&&export_u32(f,0);
    for(size_t i=0;ok&&i<minimap->cell_count;++i)ok=export_u16(f,minimap->cells[i]);
    ok=ok&&fwrite(minimap->tiles,1,minimap->tile_bytes,f)==minimap->tile_bytes&&fclose(f)==0;
    if(!ok)return 1;

    if(!export_open(&f,directory,"hud.bin"))return 1;
    ok=export_u32(f,UINT32_C(0x32445548))&&export_u32(f,(uint32_t)hud->tile_bytes)&&
       export_u32(f,(uint32_t)hud->palette_count)&&export_u32(f,(uint32_t)hud->digit_count)&&
       export_u32(f,(uint32_t)hud->clock_step_count)&&export_u32(f,(uint32_t)hud->left_cell_count)&&
       export_u32(f,(uint32_t)hud->slash_cell_count)&&export_u32(f,(uint32_t)hud->right_cell_count)&&
       export_u32(f,(uint32_t)hud->item_tile_bytes)&&
       export_u32(f,(uint32_t)hud->item_cell_count)&&export_u32(f,(uint32_t)hud->item_palette_count)&&
       fwrite(hud->tiles,1,hud->tile_bytes,f)==hud->tile_bytes;
    for(size_t i=0;ok&&i<hud->palette_count;++i)ok=export_u16(f,hud->palette[i]);
    for(size_t i=0;ok&&i<hud->digit_count;++i)ok=export_u16(f,hud->digit_tiles[i]);
    ok=ok&&fwrite(hud->clock_steps,1,hud->clock_step_count,f)==hud->clock_step_count;
    for(size_t i=0;ok&&i<hud->left_cell_count;++i)ok=export_u16(f,hud->left_cells[i]);
    for(size_t i=0;ok&&i<hud->slash_cell_count;++i)ok=export_u16(f,hud->slash_cells[i]);
    for(size_t i=0;ok&&i<hud->right_cell_count;++i)ok=export_u16(f,hud->right_cells[i]);
    ok=ok&&fwrite(hud->item_tiles,1,hud->item_tile_bytes,f)==hud->item_tile_bytes;
    for(size_t i=0;ok&&i<hud->item_cell_count;++i)ok=export_u16(f,hud->item_cells[i]);
    for(size_t i=0;ok&&i<hud->item_palette_count;++i)ok=export_u16(f,hud->item_palette_selectors[i]);
    ok=ok&&fclose(f)==0;if(!ok)return 1;

    if(!export_open(&f,directory,"manifest.json"))return 1;
    ok=fprintf(f,"{\"sections\":["
        "{\"kind\":1,\"content_id\":%u,\"flags\":3,\"path\":\"course-manifest.bin\"},"
        "{\"kind\":2,\"content_id\":1,\"flags\":3,\"path\":\"character-manifest.bin\"},"
        "{\"kind\":3,\"content_id\":%u,\"flags\":3,\"path\":\"rules.bin\"},"
        "{\"kind\":4,\"content_id\":%u,\"version\":%u,\"flags\":3,\"path\":\"route.bin\"},"
        "{\"kind\":5,\"content_id\":%u,\"flags\":3,\"path\":\"world.bin\"},"
        "{\"kind\":6,\"content_id\":%u,\"flags\":3,\"path\":\"material.bin\"},"
        "{\"kind\":8,\"content_id\":%u,\"path\":\"road.bin\"},"
        "{\"kind\":9,\"content_id\":%u,\"path\":\"sky.bin\"},"
        "{\"kind\":10,\"content_id\":1,\"path\":\"character.bin\"},"
        "{\"kind\":11,\"content_id\":%u,\"path\":\"minimap.bin\"},"
        "{\"kind\":12,\"content_id\":1,\"version\":2,\"path\":\"hud.bin\"},"
        "{\"kind\":13,\"content_id\":1,\"version\":2,\"flags\":3,\"path\":\"motion.bin\"},"
        "{\"kind\":14,\"content_id\":1,\"flags\":3,\"path\":\"collision.bin\"},"
        "{\"kind\":19,\"content_id\":1,\"path\":\"charged-exit.bin\"},"
        "{\"kind\":25,\"content_id\":%u,\"flags\":3,\"path\":\"objects.bin\"},"
        "{\"kind\":26,\"content_id\":1,\"path\":\"item-box.bin\"},"
        "{\"kind\":27,\"content_id\":%u,\"flags\":3,\"path\":\"coins.bin\"}]}\n",
        spec->course_id,spec->course_id,spec->course_id,
        spec->course_id == 24u ? 3u : 2u,spec->course_id,
        spec->course_id,spec->course_id,spec->course_id,spec->course_id,
        spec->course_id,spec->course_id)>0&&
       fclose(f)==0;
    if(ok)printf("recovered pack sections: PASS directory=%s\n",directory);
    return ok?0:1;
}

static int read_binary_file(const char *path, uint8_t **out_data,
                            size_t *out_size)
{
    FILE *file;
    long file_size;
    uint8_t *data;

    if (path == NULL || out_data == NULL || out_size == NULL)
        return 0;
    file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0 ||
        (file_size = ftell(file)) <= 0 || file_size > INT32_MAX ||
        fseek(file, 0, SEEK_SET) != 0)
    {
        if (file != NULL)
            fclose(file);
        return 0;
    }
    data = malloc((size_t)file_size);
    if (data == NULL || fread(data, 1, (size_t)file_size, file) !=
                            (size_t)file_size)
    {
        free(data);
        fclose(file);
        return 0;
    }
    fclose(file);
    *out_data = data;
    *out_size = (size_t)file_size;
    return 1;
}

typedef enum
{
    CONTENT_EXPORT_PEACH,
    CONTENT_EXPORT_BEACH,
    CONTENT_EXPORT_COURSE
} ContentExportMode;

static int export_course_content(ContentExportMode export_mode,
                                        const char *sky_path,
                                        const char *course_path,
                                        const char *character_tile_path,
                                        const char *character_palette_path,
                                        const char *rom_path,
                                        const char *output_directory,
                                        const char *metadata_path)
{
    int16_t auto_table[64] = {0};
    int16_t brake_table[8] = {0};
    int16_t brake_accelerate_table[8] = {0};
    int16_t response_curve[8] = {0};
    int16_t local_table[16] = {0};
    int16_t steering_response_data[PORTABLE_SOURCE_STEERING_RESPONSE_COUNT] =
        {0};
    int16_t steering_delta_table[10] = {0};
    int32_t sine_table[1024] = {0};
    int32_t normal_response_table_entries[
        PORTABLE_SOURCE_NORMAL_RESPONSE_COUNT];
    PortableSourceMotionTableBuffers motion_table_buffers = {
        .sine = sine_table,
        .sine_capacity = PORTABLE_SOURCE_SINE_COUNT,
        .auto_acceleration = auto_table,
        .auto_acceleration_capacity = PORTABLE_SOURCE_AUTO_ACCELERATION_COUNT,
        .brake = brake_table,
        .brake_capacity = PORTABLE_SOURCE_BRAKE_COUNT,
        .brake_accelerate = brake_accelerate_table,
        .brake_accelerate_capacity = PORTABLE_SOURCE_BRAKE_ACCELERATE_COUNT,
        .forward_response = response_curve,
        .forward_response_capacity = PORTABLE_SOURCE_FORWARD_RESPONSE_COUNT,
        .steering_delta = steering_delta_table,
        .steering_delta_capacity = PORTABLE_SOURCE_STEERING_DELTA_COUNT,
    };
    PortableSourceMotionTableView motion_table_view;
    PortableSourceNormalResponseTableBuffers normal_response_table_buffers = {
        .entries = normal_response_table_entries,
        .capacity = PORTABLE_SOURCE_NORMAL_RESPONSE_COUNT,
    };
    PortableSourceNormalResponseTableView normal_response_table;
    int32_t vector_effect_x_offsets[PORTABLE_TERRAIN_EFFECT_OFFSET_COUNT];
    int32_t vector_effect_y_offsets[PORTABLE_TERRAIN_EFFECT_OFFSET_COUNT];
    PortableTerrainEffectTableBuffers vector_effect_table_buffers = {
        .x_offsets = vector_effect_x_offsets,
        .x_capacity = PORTABLE_TERRAIN_EFFECT_OFFSET_COUNT,
        .y_offsets = vector_effect_y_offsets,
        .y_capacity = PORTABLE_TERRAIN_EFFECT_OFFSET_COUNT,
    };
    PortableTerrainEffectTableView vector_effect_tables;
    int16_t vector_alternate_x[PORTABLE_SOURCE_VECTOR_COUNT];
    int16_t vector_alternate_y[PORTABLE_SOURCE_VECTOR_COUNT];
    PortableSourceVectorTableBuffers vector_alternate_buffers = {
        .x_components = vector_alternate_x,
        .x_capacity = PORTABLE_SOURCE_VECTOR_COUNT,
        .y_components = vector_alternate_y,
        .y_capacity = PORTABLE_SOURCE_VECTOR_COUNT,
    };
    PortableSourceVectorTableView vector_alternate_view;
    int16_t normal_resultant_x_table[PORTABLE_SOURCE_B550_COUNT];
    int16_t normal_resultant_y_table[PORTABLE_SOURCE_B550_COUNT];
    PortableSourceB550TableBuffers normal_resultant_table_buffers = {
        .x_components = normal_resultant_x_table,
        .x_capacity = PORTABLE_SOURCE_B550_COUNT,
        .y_components = normal_resultant_y_table,
        .y_capacity = PORTABLE_SOURCE_B550_COUNT,
    };
    PortableSourceB550TableView normal_resultant_table_view;
    uint8_t *sky_data = NULL;
    uint8_t *course_data = NULL;
    uint8_t *character_tile_data = NULL;
    uint8_t *character_palette_data = NULL;
    uint8_t *rom_data = NULL;
    size_t sky_size;
    size_t course_size;
    size_t character_tile_size;
    size_t character_palette_size;
    size_t rom_size;
    uint8_t *owned = NULL;
    uint8_t *cells = NULL;
    uint8_t *terrain_cells = NULL;
    uint8_t *road_map_data = NULL;
    uint8_t *road_tiles = NULL;
    uint8_t *front_map = NULL;
    uint8_t *mid_map = NULL;
    uint8_t *back_map = NULL;
    uint16_t *road_palette = NULL;
    uint16_t *character_palette = NULL;
    uint16_t *minimap_cells = NULL;
    uint8_t *minimap_tiles = NULL;
    uint8_t *hud_tiles = NULL;
    uint16_t *hud_palette = NULL;
    uint16_t *hud_digit_tiles = NULL;
    uint8_t *hud_clock_steps = NULL;
    uint16_t *hud_left_cells = NULL;
    uint16_t *hud_slash_cells = NULL;
    uint16_t *hud_right_cells = NULL;
    uint8_t *hud_item_tiles = NULL;
    uint16_t *hud_item_cells = NULL;
    uint16_t hud_item_palette_selectors[PORTABLE_HUD_ITEM_PALETTE_COUNT];
    PortableTrackAsset asset;
    PortableSourceCourseMetadata course_metadata = {0};
    CourseExportAcceptance export_acceptance = {0};
    PortableTrackRaster raster;
    PortableTrackGeometryView geometry;
    PortableTrackMaterialView material;
    uint8_t runtime_material_flags[PORTABLE_TRACK_MATERIAL_ENTRY_COUNT];
    PortableTrackRuntimeMapReport runtime_map_report = {0};
    uint8_t gp_object_records[
        PORTABLE_TRACK_RUNTIME_MAX_MUTATIONS *
        PORTABLE_TRACK_STREAM_RECORD_SIZE + PORTABLE_TRACK_STREAM_RECORD_SIZE];
    PortableTrackObjectStream runtime_object_stream = {0};
    PortableTerrainAssetView terrain_asset;
    PortableTerrainView terrain;
    PortableTrackTilesetBuffers tileset_buffers;
    PortableTrackTilesetView tileset;
    PortableRoadMapBuffers road_map_buffers;
    PortableRoadMapView course_map={0};
    PortableMinimapAssetBuffers minimap_buffers;
    PortableMinimapAssetView minimap;
    PortableHudAssetBuffers hud_buffers;
    PortableHudAssetView hud_asset;
    PortableSkyAssetBuffers sky_buffers = {0};
    PortableSkyAssetView sky;
    PortableCharacterAssetBuffers character_buffers = {0};
    PortableCharacterAssetView character;
    PortableDriverPilotDependencies dependencies = {0};
    PortableTrackObjectStream object_stream = {0};
    PortableTrackObjectStream item_box_stream = {0};
    PortableTrackObjectStream coin_stream = {0};
    int result = 1;
    uint32_t selected_header = export_mode == CONTENT_EXPORT_BEACH ? 5u : 4u;
    uint32_t selected_course = 0u;
    uint32_t selected_background = 0u;
    uint16_t selected_checkpoints = 0u;

    if (export_mode == CONTENT_EXPORT_COURSE)
    {
        if (!read_course_export_acceptance(metadata_path,
                                           &export_acceptance))
        {
            fprintf(stderr, "invalid course export acceptance file\n");
            goto done;
        }
        selected_header = export_acceptance.header_index;
    }

    if (!read_binary_file(sky_path, &sky_data, &sky_size) ||
        !read_binary_file(course_path, &course_data, &course_size) ||
        !read_binary_file(character_tile_path, &character_tile_data,
                           &character_tile_size) ||
        !read_binary_file(character_palette_path, &character_palette_data,
                          &character_palette_size) ||
        !read_binary_file(rom_path, &rom_data, &rom_size))
        goto done;
    PortableSourceCourseStatus course_status =
        portable_source_course_selection(rom_data, rom_size, selected_header,
                                         &selected_course,
                                         &selected_background);
    if (course_status != PORTABLE_SOURCE_COURSE_OK)
    {
        fprintf(stderr, "source course selection failed: %s\n",
                portable_source_course_status_name(course_status));
        goto done;
    }
    if (portable_source_motion_tables_build(
            rom_data, rom_size, &motion_table_buffers, &motion_table_view) !=
        PORTABLE_SOURCE_MOTION_TABLES_OK)
    {
        fprintf(stderr, "motion table build failed\n");
        goto done;
    }
    if (portable_source_normal_response_table_build(
            rom_data, rom_size, &normal_response_table_buffers,
            &normal_response_table) !=
        PORTABLE_SOURCE_NORMAL_RESPONSE_TABLES_OK)
    {
        fprintf(stderr, "normal response table build failed\n");
        goto done;
    }
    if (portable_terrain_effect_tables_build(
            rom_data, rom_size, &vector_effect_table_buffers,
            &vector_effect_tables) != PORTABLE_TERRAIN_EFFECT_TABLES_OK)
    {
        fprintf(stderr, "vector effect table build failed\n");
        goto done;
    }
    if (portable_source_vector_tables_build(
            rom_data, rom_size, &vector_alternate_buffers,
            &vector_alternate_view) != PORTABLE_SOURCE_VECTOR_TABLES_OK)
    {
        fprintf(stderr, "alternate vector table build failed\n");
        goto done;
    }
    if (portable_source_b550_tables_build(
            rom_data, rom_size, &normal_resultant_table_buffers,
            &normal_resultant_table_view) != PORTABLE_SOURCE_B550_TABLES_OK)
    {
        fprintf(stderr, "normal resultant table build failed\n");
        goto done;
    }
    if (portable_source_local_table_build(
            rom_data, rom_size, 3u, 2, local_table,
            PORTABLE_SOURCE_LOCAL_TABLE_COUNT) !=
        PORTABLE_SOURCE_MOTION_TABLES_OK)
    {
        fprintf(stderr, "source local table build failed\n");
        goto done;
    }
    if (portable_source_steering_response_build(
            rom_data, rom_size, 3, 0, steering_response_data,
            PORTABLE_SOURCE_STEERING_RESPONSE_COUNT) !=
        PORTABLE_SOURCE_MOTION_TABLES_OK)
    {
        fprintf(stderr, "source steering response build failed\n");
        goto done;
    }

    owned = malloc(course_size);
    cells = malloc(128u * 128u);
    terrain_cells = malloc(0x10000);
    road_map_data = malloc(PORTABLE_ROAD_COURSE_MAP_STORAGE_BYTES);
    road_tiles = malloc(PORTABLE_TRACK_TILESET_TILE_BYTES);
    road_palette = malloc(PORTABLE_ROAD_PALETTE_COUNT *
                          sizeof(*road_palette));
    sky_buffers.tiles = malloc(0x2800);
    sky_buffers.palette = malloc(63 * sizeof(*sky_buffers.palette));
    sky_buffers.front_map = front_map = malloc(0x800);
    sky_buffers.mid_map = mid_map = malloc(0x1000);
    sky_buffers.back_map = back_map = malloc(0x800);
    character_palette = malloc(PORTABLE_CHARACTER_PALETTE_COUNT *
                               sizeof(*character_palette));
    minimap_cells = malloc(PORTABLE_MINIMAP_CELL_COUNT *
                           sizeof(*minimap_cells));
    minimap_tiles = malloc(PORTABLE_MINIMAP_TILE_BYTES);
    hud_tiles = malloc(PORTABLE_HUD_CHAR_BYTES);
    hud_palette = malloc(PORTABLE_HUD_PALETTE_COUNT * sizeof(*hud_palette));
    hud_digit_tiles =
        malloc(PORTABLE_HUD_DIGIT_COUNT * sizeof(*hud_digit_tiles));
    hud_clock_steps = malloc(PORTABLE_HUD_CLOCK_STEP_COUNT);
    hud_left_cells =
        malloc(PORTABLE_HUD_LEFT_CELL_COUNT * sizeof(*hud_left_cells));
    hud_slash_cells =
        malloc(PORTABLE_HUD_SLASH_CELL_COUNT * sizeof(*hud_slash_cells));
    hud_right_cells =
        malloc(PORTABLE_HUD_RIGHT_CELL_COUNT * sizeof(*hud_right_cells));
    hud_item_tiles = malloc(PORTABLE_HUD_ITEM_TILE_BYTES);
    hud_item_cells =
        malloc(PORTABLE_HUD_ITEM_CELL_COUNT * sizeof(*hud_item_cells));
    if (owned == NULL || cells == NULL || terrain_cells == NULL ||
        road_map_data == NULL || road_tiles == NULL || road_palette == NULL ||
        sky_buffers.tiles == NULL || sky_buffers.palette == NULL ||
        front_map == NULL || mid_map == NULL || back_map == NULL ||
        character_palette == NULL || minimap_cells == NULL ||
        minimap_tiles == NULL || hud_tiles == NULL || hud_palette == NULL ||
        hud_digit_tiles == NULL || hud_clock_steps == NULL ||
        hud_left_cells == NULL ||
        hud_slash_cells == NULL || hud_right_cells == NULL ||
        hud_item_tiles == NULL || hud_item_cells == NULL)
        goto done;

    if (portable_track_asset_import(course_data, course_size, selected_header,
                                    selected_course, owned,
                                    course_size, &asset) !=
            PORTABLE_TRACK_ASSET_OK)
        goto done;
    if (portable_track_object_stream_open(
            &asset, PORTABLE_TRACK_STREAM_SECONDARY_OBJECT,
            &item_box_stream) != PORTABLE_TRACK_OBJECT_ASSET_OK)
        goto done;
    if (portable_track_object_stream_open(
            &asset, PORTABLE_TRACK_STREAM_COIN,
            &coin_stream) != PORTABLE_TRACK_OBJECT_ASSET_OK)
        goto done;
    course_status = portable_source_course_metadata_build(
        rom_data, rom_size, selected_header, &asset, &course_metadata);
    if (course_status != PORTABLE_SOURCE_COURSE_OK)
    {
        fprintf(stderr, "source course metadata failed: %s\n",
                portable_source_course_status_name(course_status));
        goto done;
    }
    selected_checkpoints = course_metadata.checkpoint_count;
    if (portable_track_raster_build(&asset, cells, 128u * 128u, &raster) !=
            PORTABLE_TRACK_RASTER_OK ||
        portable_track_geometry_init(&asset, selected_checkpoints, &geometry) !=
            PORTABLE_TRACK_GEOMETRY_OK ||
        portable_track_material_init(&asset, &material) !=
            PORTABLE_TRACK_MATERIAL_OK ||
        portable_terrain_asset_build(&asset, terrain_cells, 0x10000,
                                     &terrain_asset) !=
                                     PORTABLE_TERRAIN_ASSET_OK)
        goto done;
    terrain = terrain_asset.view;

    tileset_buffers.tiles = road_tiles;
    tileset_buffers.tile_capacity = PORTABLE_TRACK_TILESET_TILE_BYTES;
    tileset_buffers.palette = road_palette;
    tileset_buffers.palette_capacity = PORTABLE_TRACK_TILESET_PALETTE_COUNT;
    road_map_buffers.map = road_map_data;
    road_map_buffers.map_capacity = PORTABLE_ROAD_COURSE_MAP_STORAGE_BYTES;
    if (portable_track_tileset_asset_build(&asset, &tileset_buffers, &tileset) !=
            PORTABLE_TRACK_TILESET_OK ||
        portable_track_tileset_asset_load_common(
            rom_data, rom_size, 0x76c1c, road_tiles,
            PORTABLE_TRACK_TILESET_TILE_BYTES) != PORTABLE_TRACK_TILESET_OK ||
        portable_road_map_asset_build_course(&asset, &road_map_buffers,
                                             &course_map) != PORTABLE_ROAD_MAP_OK)
        goto done;
    if (portable_track_object_stream_open(
            &asset, PORTABLE_TRACK_STREAM_OBJECT, &object_stream) !=
            PORTABLE_TRACK_OBJECT_ASSET_OK)
        goto done;
    runtime_object_stream = object_stream;
    if (export_mode == CONTENT_EXPORT_COURSE && selected_header == 5u)
    {
        size_t selected = 0u;
        if (object_stream.record_count > PORTABLE_TRACK_RUNTIME_MAX_MUTATIONS)
            goto done;
        for (uint32_t index = 0u; index < object_stream.record_count; ++index)
        {
            uint8_t record[PORTABLE_TRACK_STREAM_RECORD_SIZE];
            PortableTrackObjectDecode decoded = {0};
            if (portable_track_object_stream_read(&object_stream, index,
                    record) != PORTABLE_TRACK_OBJECT_ASSET_OK ||
                portable_track_object_decode(rom_data, rom_size, 5u,
                    record[0], &decoded) != PORTABLE_TRACK_OBJECT_DECODER_OK)
                goto done;
            if (!portable_track_object_uses_runtime_map_alias(
                    decoded.source_code))
                continue;
            memcpy(gp_object_records + selected *
                       PORTABLE_TRACK_STREAM_RECORD_SIZE,
                   record, PORTABLE_TRACK_STREAM_RECORD_SIZE);
            ++selected;
        }
        memset(gp_object_records + selected *
                   PORTABLE_TRACK_STREAM_RECORD_SIZE, 0,
               PORTABLE_TRACK_STREAM_RECORD_SIZE);
        runtime_object_stream.data = gp_object_records;
        runtime_object_stream.size = (selected + 1u) *
            PORTABLE_TRACK_STREAM_RECORD_SIZE;
        runtime_object_stream.field_offset = 0u;
        runtime_object_stream.stream_offset = 0u;
        runtime_object_stream.terminator_offset = (uint32_t)(selected *
            PORTABLE_TRACK_STREAM_RECORD_SIZE);
        runtime_object_stream.record_count = (uint32_t)selected;
    }
    if (portable_track_runtime_map_apply_object_stream(
            &runtime_object_stream, terrain_cells, terrain.width * terrain.height,
            terrain.width, terrain.height, road_tiles,
            PORTABLE_TRACK_TILESET_TILE_BYTES, &material,
            runtime_material_flags, sizeof(runtime_material_flags),
            &runtime_map_report) != PORTABLE_TRACK_RUNTIME_MAP_OK)
        goto done;
    material.flags = runtime_material_flags;
    if (portable_road_palette_load_fixed(
            rom_data, rom_size, 0x76de8,
            road_palette + PORTABLE_ROAD_PALETTE_COURSE_COUNT,
            PORTABLE_ROAD_PALETTE_FIXED_COUNT) != PORTABLE_ROAD_PALETTE_OK)
        goto done;
    tileset.palette_count = PORTABLE_ROAD_PALETTE_COUNT;
    minimap_buffers.cells = minimap_cells;
    minimap_buffers.cell_capacity = PORTABLE_MINIMAP_CELL_COUNT;
    minimap_buffers.tiles = minimap_tiles;
    minimap_buffers.tile_capacity = PORTABLE_MINIMAP_TILE_BYTES;
    if (portable_minimap_asset_build_with_tiles(
            rom_data, rom_size, 0xee76c, 0x66c0c, &minimap_buffers, &minimap) !=
        PORTABLE_MINIMAP_ASSET_OK)
        goto done;
    hud_buffers.tiles = hud_tiles;
    hud_buffers.tile_capacity = PORTABLE_HUD_CHAR_BYTES;
    hud_buffers.palette = hud_palette;
    hud_buffers.palette_capacity = PORTABLE_HUD_PALETTE_COUNT;
    hud_buffers.digit_tiles = hud_digit_tiles;
    hud_buffers.digit_capacity = PORTABLE_HUD_DIGIT_COUNT;
    hud_buffers.clock_steps = hud_clock_steps;
    hud_buffers.clock_capacity = PORTABLE_HUD_CLOCK_STEP_COUNT;
    hud_buffers.left_cells = hud_left_cells;
    hud_buffers.left_capacity = PORTABLE_HUD_LEFT_CELL_COUNT;
    hud_buffers.slash_cells = hud_slash_cells;
    hud_buffers.slash_capacity = PORTABLE_HUD_SLASH_CELL_COUNT;
    hud_buffers.right_cells = hud_right_cells;
    hud_buffers.right_capacity = PORTABLE_HUD_RIGHT_CELL_COUNT;
    hud_buffers.item_tiles = hud_item_tiles;
    hud_buffers.item_tile_capacity = PORTABLE_HUD_ITEM_TILE_BYTES;
    hud_buffers.item_cells = hud_item_cells;
    hud_buffers.item_cell_capacity = PORTABLE_HUD_ITEM_CELL_COUNT;
    hud_buffers.item_palette_selectors=hud_item_palette_selectors;
    hud_buffers.item_palette_capacity=PORTABLE_HUD_ITEM_PALETTE_COUNT;
    if (portable_hud_asset_build(
            rom_data, rom_size, 0x62834, 0x62774, 0xee67e, 0xee6a6,
            0xee7a8, 0xeef84, 0xee7b8, 0x6f33c, 0xee7c8, 0xee65c,
            &hud_buffers, &hud_asset) !=
        PORTABLE_HUD_ASSET_OK)
        goto done;

    sky_buffers.tile_capacity = 0x2800;
    sky_buffers.palette_capacity = 63;
    sky_buffers.front_capacity = 0x800;
    sky_buffers.mid_capacity = 0x1000;
    sky_buffers.back_capacity = 0x800;
    character_buffers.palette = character_palette;
    character_buffers.palette_capacity = PORTABLE_CHARACTER_PALETTE_COUNT;
    if (portable_sky_asset_build(sky_data, sky_size, selected_background,
                                 &sky_buffers, &sky) !=
            PORTABLE_SKY_ASSET_OK ||
        portable_character_asset_build(
            character_tile_data, character_tile_size, character_palette_data,
            character_palette_size, 2, 0x18, &character_buffers, &character) !=
            PORTABLE_CHARACTER_ASSET_OK)
        goto done;
    dependencies.auto_acceleration_table = motion_table_view.auto_acceleration;
    dependencies.auto_acceleration_count =
        motion_table_view.auto_acceleration_count;
    dependencies.brake_table = motion_table_view.brake;
    dependencies.brake_table_count = motion_table_view.brake_count;
    dependencies.brake_accelerate_table = motion_table_view.brake_accelerate;
    dependencies.brake_accelerate_table_count =
        motion_table_view.brake_accelerate_count;
    dependencies.forward_response_curve = motion_table_view.forward_response;
    dependencies.forward_response_count = motion_table_view.forward_response_count;
    dependencies.forward_local_table = local_table;
    dependencies.forward_local_count = 16;
    dependencies.steering_delta_table = motion_table_view.steering_delta;
    dependencies.steering_delta_count = motion_table_view.steering_delta_count;
    dependencies.steering_response_data = steering_response_data;
    dependencies.steering_response_data_count =
        PORTABLE_SOURCE_STEERING_RESPONSE_COUNT;
    dependencies.sine_table = motion_table_view.sine;
    dependencies.sine_count = motion_table_view.sine_count;
    dependencies.raster = &raster;
    dependencies.track_material = &material;
    dependencies.terrain = &terrain;
    dependencies.terrain_effect_tables = &vector_effect_tables;
    dependencies.vector_alternate_x_components =
        vector_alternate_view.x_components;
    dependencies.vector_alternate_x_count = vector_alternate_view.x_count;
    dependencies.vector_alternate_y_components =
        vector_alternate_view.y_components;
    dependencies.vector_alternate_y_count = vector_alternate_view.y_count;
    dependencies.normal_resultant_x_table =
        normal_resultant_table_view.x_components;
    dependencies.normal_resultant_x_count = normal_resultant_table_view.x_count;
    dependencies.normal_resultant_y_table =
        normal_resultant_table_view.y_components;
    dependencies.normal_resultant_y_count = normal_resultant_table_view.y_count;
    dependencies.normal_response_table = &normal_response_table;

    if (export_mode == CONTENT_EXPORT_PEACH || export_mode == CONTENT_EXPORT_BEACH || export_mode == CONTENT_EXPORT_COURSE)
    {
        CourseExportAcceptance accepted = export_acceptance;
        if (export_mode == CONTENT_EXPORT_PEACH)
            accepted = (CourseExportAcceptance){4u, 10108u, 7424u, 7147u,
                                                7133u, 8152u};
        if (export_mode == CONTENT_EXPORT_BEACH)
            accepted = (CourseExportAcceptance){5u, 14256u, 14046u, 8085u,
                                                0u, 0u};
        const int source_beach = course_metadata.course_id == 25u;
        const RecoveredExportSpec spec = {
            .course_id = course_metadata.course_id,
            .course_revision = source_beach ? 3u : 1u,
            .rules_id = source_beach ? 3u : 1u,
            .rules_revision = 1u,
            .state_schema_version = source_beach ? 3u : 1u,
            .start_x_q16 = course_metadata.start_x_q16,
            .start_y_q16 = course_metadata.start_y_q16,
            .start_route_index = course_metadata.start_route_index,
            .start_checkpoint_raw = source_beach
                ? (uint8_t)(UINT8_C(0x80) | course_metadata.start_route_index)
                : 0u,
            .progress_kind = source_beach
                ? PORTABLE_RACE_PROGRESS_SOURCE_RASTER_V1
                : PORTABLE_RACE_PROGRESS_DEMO_V1,
            .finish_kind = source_beach
                ? PORTABLE_FINISH_SOURCE_Y_CROSSING
                : PORTABLE_FINISH_NORTHBOUND_Y,
            .finish_y_q16 = course_metadata.finish_y_q16,
            .expected_recovered_ticks = accepted.expected_recovered_ticks,
            .expected_recovered_race_ticks =
                accepted.expected_recovered_race_ticks,
            .expected_demo_ticks = accepted.expected_demo_ticks,
            .expected_demo_left_ticks = accepted.expected_demo_left_ticks,
            .expected_demo_right_ticks = accepted.expected_demo_right_ticks};
        result = export_recovered_sections(output_directory, &geometry, &dependencies,
                                           &item_box_stream,&coin_stream,
                                           (uint8_t)selected_header,
                                           &tileset, &sky, &character,
                                           &minimap, &hud_asset, &spec,
                                           rom_data,rom_size);
        goto done;
    }

done:
    free(character_palette);
    free(sky_buffers.back_map);
    free(sky_buffers.mid_map);
    free(sky_buffers.front_map);
    free(sky_buffers.palette);
    free(sky_buffers.tiles);
    free(road_palette);
    free(road_tiles);
    free(road_map_data);
    free(minimap_cells);
    free(minimap_tiles);
    free(hud_right_cells);
    free(hud_item_cells);
    free(hud_item_tiles);
    free(hud_slash_cells);
    free(hud_left_cells);
    free(hud_clock_steps);
    free(hud_digit_tiles);
    free(hud_palette);
    free(hud_tiles);
    free(terrain_cells);
    free(cells);
    free(owned);
    free(rom_data);
    free(character_palette_data);
    free(character_tile_data);
    free(course_data);
    free(sky_data);
    return result;
}

int portable_content_import_command(int argc, char **argv)
{
    ContentExportMode mode;
    if (argc == 8 && strcmp(argv[1], "--export-recovered-pack-sections") == 0)
        mode = CONTENT_EXPORT_PEACH;
    else if (argc == 8 && strcmp(argv[1], "--export-shy-guy-pack-sections") == 0)
        mode = CONTENT_EXPORT_BEACH;
    else if (argc == 9 && strcmp(argv[1], "--export-course-pack-sections") == 0)
        mode = CONTENT_EXPORT_COURSE;
    else return -1;
    return export_course_content(mode, argv[2], argv[3], argv[4], argv[5],
                                 argv[6], argv[7],
                                 argc == 9 ? argv[8] : NULL);
}
