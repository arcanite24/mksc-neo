#include "character_renderer.h"
#include "artwork_lod.h"

static uint32_t color_to_rgb(uint16_t color)
{
    const uint32_t red5 = color & 0x1fu;
    const uint32_t green5 = (color >> 5) & 0x1fu;
    const uint32_t blue5 = (color >> 10) & 0x1fu;
    const uint32_t red = (red5 << 3u) | (red5 >> 2u);
    const uint32_t green = (green5 << 3u) | (green5 >> 2u);
    const uint32_t blue = (blue5 << 3u) | (blue5 >> 2u);

    return (red << 16) | (green << 8) | blue;
}

static int render_affine_axes(
    const PortableCharacterAssetView *character,
    PortableSoftwareFrame *frame,
    int32_t origin_x,
    int32_t origin_y,
    int flip_x,
    int flip_y,
    uint16_t scale_x, uint16_t scale_y, int bottom_anchor)
{
    uint32_t width;
    uint32_t height;
    int32_t destination_origin_x;
    int32_t destination_origin_y;

    if (character == NULL || frame == NULL || frame->pixels == NULL ||
        character->tiles == NULL || character->palette == NULL ||
        character->tile_bytes < PORTABLE_CHARACTER_FRAME_BYTES ||
        character->palette_count < PORTABLE_CHARACTER_PALETTE_COUNT ||
        character->width != PORTABLE_CHARACTER_WIDTH ||
        character->height != PORTABLE_CHARACTER_HEIGHT)
        return 0;
    const uint32_t output_scale=(uint32_t)portable_software_frame_scale(frame);
    if (scale_x == 0u) scale_x=UINT16_C(0x0100);
    if (scale_y == 0u) scale_y=UINT16_C(0x0100);
    width = (PORTABLE_CHARACTER_WIDTH * UINT32_C(0x100) * output_scale +
             scale_x - 1u) / scale_x;
    height = (PORTABLE_CHARACTER_HEIGHT * UINT32_C(0x100) * output_scale +
              scale_y - 1u) / scale_y;
    if (width == 0u || height == 0u)
        return 0;
    destination_origin_x = origin_x * (int32_t)output_scale +
        ((int32_t)(PORTABLE_CHARACTER_WIDTH * output_scale) - (int32_t)width) / 2;
    destination_origin_y = origin_y * (int32_t)output_scale +
        ((int32_t)(PORTABLE_CHARACTER_HEIGHT * output_scale) - (int32_t)height) / (bottom_anchor?1:2);

    const int64_t physical_width=(int64_t)frame->width*output_scale;
    const int64_t physical_height=(int64_t)frame->height*output_scale;
    const uint32_t first_x=destination_origin_x<0?(uint32_t)(-(int64_t)destination_origin_x):0u;
    const uint32_t first_y=destination_origin_y<0?(uint32_t)(-(int64_t)destination_origin_y):0u;
    const int64_t available_x=physical_width-destination_origin_x;
    const int64_t available_y=physical_height-destination_origin_y;
    if(available_x<=0 || available_y<=0)return 1;
    const uint32_t last_x=(uint64_t)available_x<width?(uint32_t)available_x:width;
    const uint32_t last_y=(uint64_t)available_y<height?(uint32_t)available_y:height;
    for (uint32_t y = first_y; y < last_y; ++y)
    {
        for (uint32_t x = first_x; x < last_x; ++x)
        {
            size_t source_x = (size_t)x * scale_x / (256u * output_scale);
            size_t source_y = (size_t)y * scale_y / (256u * output_scale);
            source_x = flip_x ? PORTABLE_CHARACTER_WIDTH - 1u - source_x
                              : source_x;
            source_y = flip_y ? PORTABLE_CHARACTER_HEIGHT - 1u - source_y
                              : source_y;
            size_t tile = (source_y / 8u) * 8u + source_x / 8u;
            size_t byte_offset = tile * 32u + (source_y & 7u) * 4u +
                                 (source_x & 7u) / 2u;
            uint8_t packed = character->tiles[byte_offset];
            size_t color_index = (source_x & 1u) != 0 ? packed >> 4
                                                      : packed & 0x0fu;
            int32_t destination_x = destination_origin_x + (int32_t)x;
            int32_t destination_y = destination_origin_y + (int32_t)y;

            if (color_index == 0 || destination_x < 0 || destination_y < 0 ||
                destination_x >= (int32_t)(frame->width * output_scale) ||
                destination_y >= (int32_t)(frame->height * output_scale))
                continue;
            portable_software_frame_store_rgb_physical(frame,(size_t)destination_x,(size_t)destination_y, color_to_rgb(character->palette[color_index]));
        }
    }
    return 1;
}

int portable_character_render_affine(
    const PortableCharacterAssetView *character, PortableSoftwareFrame *frame,
    int32_t origin_x, int32_t origin_y, int flip_x, int flip_y, uint16_t scale)
{
    return render_affine_axes(character,frame,origin_x,origin_y,flip_x,flip_y,scale,scale,0);
}

int portable_character_render(const PortableCharacterAssetView *character,
                              PortableSoftwareFrame *frame,
                              int32_t origin_x,
                              int32_t origin_y,
                              int flip_x,
                              int flip_y)
{
    return portable_character_render_affine(character, frame, origin_x,
                                            origin_y, flip_x, flip_y,
                                            UINT16_C(0x0100));
}

int portable_character_render_view(
    const PortableCharacterAssetView *character,
    PortableSoftwareFrame *frame,
    int32_t origin_x,
    int32_t origin_y,
    uint8_t view_angle,
    int flip_y)
{
    PortableCharacterAssetView selected;
    size_t table_index;
    uint8_t frame_slot;

    if (character == NULL || view_angle > 127u ||
        character->direction_tiles == NULL ||
        character->view_frame_slots == NULL ||
        character->direction_tile_bytes < PORTABLE_CHARACTER_FRAME_BYTES ||
        character->view_frame_slot_count < PORTABLE_CHARACTER_VIEW_SLOT_COUNT)
        return 0;
    table_index = view_angle > 63u ? 127u - view_angle : view_angle;
    frame_slot = character->view_frame_slots[table_index];
    if (frame_slot >= character->direction_frame_count ||
        (size_t)frame_slot * PORTABLE_CHARACTER_FRAME_BYTES >
            character->direction_tile_bytes - PORTABLE_CHARACTER_FRAME_BYTES)
        return 0;
    selected = *character;
    selected.tiles = character->direction_tiles +
                     (size_t)frame_slot * PORTABLE_CHARACTER_FRAME_BYTES;
    selected.tile_bytes = PORTABLE_CHARACTER_FRAME_BYTES;
    selected.frame_slot = frame_slot;
    return portable_character_render(&selected, frame, origin_x, origin_y,
                                     view_angle > 63u, flip_y);
}

int portable_character_render_view_scaled(
    const PortableCharacterAssetView *character,
    PortableSoftwareFrame *frame,
    int32_t origin_x,
    int32_t origin_y,
    uint8_t view_angle,
    int flip_y,
    uint16_t scale_x, uint16_t scale_y, int bottom_anchor)
{
    PortableCharacterAssetView selected;
    size_t table_index;
    uint8_t frame_slot;

    if (character == NULL || view_angle > 127u ||
        character->direction_tiles == NULL ||
        character->view_frame_slots == NULL ||
        character->direction_tile_bytes < PORTABLE_CHARACTER_FRAME_BYTES ||
        character->view_frame_slot_count < PORTABLE_CHARACTER_VIEW_SLOT_COUNT)
        return 0;
    table_index = view_angle > 63u ? 127u - view_angle : view_angle;
    frame_slot = character->view_frame_slots[table_index];
    if (frame_slot >= character->direction_frame_count ||
        (size_t)frame_slot * PORTABLE_CHARACTER_FRAME_BYTES >
            character->direction_tile_bytes - PORTABLE_CHARACTER_FRAME_BYTES)
        return 0;
    selected = *character;
    selected.tiles = character->direction_tiles +
                     (size_t)frame_slot * PORTABLE_CHARACTER_FRAME_BYTES;
    selected.tile_bytes = PORTABLE_CHARACTER_FRAME_BYTES;
    selected.frame_slot = frame_slot;
    return render_affine_axes(
        &selected, frame, origin_x, origin_y, view_angle > 63u, flip_y,
        scale_x,scale_y,bottom_anchor);
}

int portable_character_render_view_affine(
    const PortableCharacterAssetView *character, PortableSoftwareFrame *frame,
    int32_t origin_x, int32_t origin_y, uint8_t view_angle, int flip_y, uint16_t scale)
{
    return portable_character_render_view_scaled(character,frame,origin_x,origin_y,
        view_angle,flip_y,scale,scale,0);
}

int portable_character_render_view_oam_affine(
    const PortableCharacterAssetView *character,
    PortableSoftwareFrame *frame,
    int32_t origin_x,
    int32_t origin_y,
    uint8_t view_angle,
    int16_t pa,
    int16_t pd,
    int16_t sample_center_y)
{
    return portable_character_render_view_oam_lod(
        character, frame, origin_x, origin_y, view_angle, 64u,
        pa, pd, sample_center_y, 0);
}


int portable_character_render_view_oam_lod(
    const PortableCharacterAssetView *character,
    PortableSoftwareFrame *frame,
    int32_t origin_x,
    int32_t origin_y,
    uint8_t view_angle,
    uint8_t obj_size,
    int16_t pa,
    int16_t pd,
    int16_t sample_center_y,
    int floor_sample)
{
    size_t table_index;
    if(character==NULL||view_angle>127u||
       character->view_frame_slots==NULL||
       character->view_frame_slot_count<PORTABLE_CHARACTER_VIEW_SLOT_COUNT)
        return 0;
    table_index = view_angle > 63u ? 127u - view_angle : view_angle;
    return portable_character_render_slot_oam_lod(
        character,frame,origin_x,origin_y,
        character->view_frame_slots[table_index],obj_size,pa,pd,
        sample_center_y,floor_sample);
}

/* Geometry remains in the source cell's16/32/64 units. Enhanced output can
 * resolve a finer existing view texture without changing its pivot or matrix. */
static const uint8_t *character_lod_texture(const PortableCharacterAssetView *character,
    uint8_t slot,int obj_size,int output_scale,int16_t pa,int16_t pd,uint8_t policy,int *texture_size)
{
    int desired=obj_size;
    const int low_frames=slot<PORTABLE_CHARACTER_LOD_FRAME_COUNT;
    if(policy){
        const int ax=pa<0?-(int)pa:pa,ay=pd<0?-(int)pd:pd;
        const int inverse=ax<ay?ax:ay;
        const unsigned footprint=inverse?
            (unsigned)((obj_size*256*output_scale+inverse-1)/inverse):64u;
        const unsigned sizes[3]={low_frames&&character->lod16_tiles?16u:0u,
            low_frames&&character->lod32_tiles?32u:0u,64u};
        const size_t source=obj_size==16?0u:obj_size==32?1u:2u;
        desired=(int)sizes[portable_artwork_lod_select(policy,sizes,3u,source,footprint)];
    }else if(output_scale>1){
        const int ax=pa<0?-(int)pa:pa,ay=pd<0?-(int)pd:pd;
        const int inverse=ax<ay?ax:ay;
        const int footprint=inverse?(obj_size*256*output_scale+inverse-1)/inverse:64;
        while(desired<64 && footprint>desired)desired*=2;
    }
    if(desired==16 && low_frames && character->lod16_tiles){
        *texture_size=16;
        return character->lod16_tiles+(size_t)slot*PORTABLE_CHARACTER_LOD16_FRAME_BYTES;
    }
    if((desired==32 || (output_scale>1 && desired==16)) && low_frames && character->lod32_tiles){
        *texture_size=32;
        return character->lod32_tiles+(size_t)slot*PORTABLE_CHARACTER_LOD32_FRAME_BYTES;
    }
    *texture_size=64;
    return character->direction_tiles+(size_t)slot*PORTABLE_CHARACTER_FRAME_BYTES;
}

/* A policy does not change sampling when sparse art resolves to the same
 * texture as Original. Only an actual detail substitution needs finer1x UVs. */
static int character_lod_texture_changed(const PortableCharacterAssetView *character,
    uint8_t slot,int obj_size,int output_scale,int16_t pa,int16_t pd,
    const uint8_t *tiles,int texture_size,uint8_t policy)
{
    if(!policy || output_scale!=1)return 0;
    int original_size;
    const uint8_t *original=character_lod_texture(character,slot,obj_size,
        output_scale,pa,pd,0u,&original_size);
    return tiles!=original || texture_size!=original_size;
}

static int character_lod_sample(int native_sample,int delta,int coefficient,
    int obj_size,int texture_size,int output_scale,int floor_sample,int finer_texture)
{
    const int ratio=texture_size/obj_size;
    if(output_scale==1 && !finer_texture)return native_sample*ratio;
    const int64_t value=(int64_t)delta*coefficient*ratio;
    const int divisor=256*output_scale;
    return texture_size/2+(int)(floor_sample?
        portable_software_frame_floor_div(value,divisor):value/divisor);
}

static int render_slot_oam_lod(
    const PortableCharacterAssetView *character,
    PortableSoftwareFrame *frame,
    int32_t origin_x,
    int32_t origin_y,
    uint8_t frame_slot,
    uint8_t obj_size,
    int16_t pa,
    int16_t pd,
    int16_t sample_center_y,
    int floor_sample,int flip_source_x,int physical_origin)
{
    const uint8_t *tiles;
    if (character == NULL || frame == NULL || frame->pixels == NULL ||
        character->direction_tiles == NULL || character->palette == NULL ||
        character->palette_count < PORTABLE_CHARACTER_PALETTE_COUNT ||
        character->direction_tile_bytes < PORTABLE_CHARACTER_FRAME_BYTES ||
        (obj_size!=16u&&obj_size!=32u&&obj_size!=64u))
        return 0;
    if (frame_slot >= character->direction_frame_count ||
        (size_t)frame_slot * PORTABLE_CHARACTER_FRAME_BYTES >
            character->direction_tile_bytes - PORTABLE_CHARACTER_FRAME_BYTES)
        return 0;
    const int output_scale=(int)portable_software_frame_scale(frame);
    int texture_size;
    tiles=character_lod_texture(character,frame_slot,obj_size,output_scale,pa,pd,frame->artwork_lod,&texture_size);
    const int finer_texture=character_lod_texture_changed(character,frame_slot,
        obj_size,output_scale,pa,pd,tiles,texture_size,frame->artwork_lod);
    for (int y = 0; y < obj_size*output_scale; ++y) {
        const int32_t target_y = origin_y*(physical_origin?1:output_scale) + y;
        const int sample_y = obj_size/2 +
            (floor_sample
                ? (int)portable_software_frame_floor_div((y - sample_center_y*output_scale) * pd,256*output_scale)
                : ((y - sample_center_y*output_scale) * pd) / (256*output_scale));
        const int source_y=character_lod_sample(sample_y,y-sample_center_y*output_scale,
            pd,obj_size,texture_size,output_scale,floor_sample,finer_texture);
        if (target_y < 0 || target_y >= (int32_t)frame->height*output_scale ||
            sample_y < 0 || sample_y >= obj_size || source_y<0 || source_y>=texture_size)
            continue;
        for (int x = 0; x < obj_size*output_scale; ++x) {
            const int32_t target_x = origin_x*(physical_origin?1:output_scale) + x;
            const int sample_x = obj_size/2 +
                (floor_sample
                    ? (int)portable_software_frame_floor_div((x - obj_size/2*output_scale) * pa,256*output_scale)
                    : ((x - obj_size/2*output_scale) * pa) / (256*output_scale));
            const int sampled_x=character_lod_sample(sample_x,x-obj_size/2*output_scale,
                pa,obj_size,texture_size,output_scale,floor_sample,finer_texture);
            const int source_x=flip_source_x?
                (output_scale==1 && !finer_texture?(obj_size-1-sample_x)*(texture_size/obj_size):texture_size-1-sampled_x):sampled_x;
            size_t tile;
            size_t offset;
            uint8_t packed;
            uint8_t color_index;
            if (target_x < 0 || target_x >= (int32_t)frame->width*output_scale ||
                sample_x < 0 || sample_x >= obj_size || source_x<0 || source_x>=texture_size)
                continue;
            tile = (size_t)(source_y / 8) *
                   (size_t)(texture_size/8) +
                   (size_t)(source_x / 8);
            offset = tile * 32u + (size_t)(source_y & 7) * 4u +
                     (size_t)(source_x & 7) / 2u;
            packed = tiles[offset];
            color_index = (source_x & 1) != 0 ? packed >> 4 : packed & 15u;
            if (color_index != 0u)
                portable_software_frame_store_rgb_physical(frame,(size_t)target_x,(size_t)target_y, color_to_rgb(character->palette[color_index]));
        }
    }
    return 1;
}

int portable_character_render_slot_oam_lod(
    const PortableCharacterAssetView *character,PortableSoftwareFrame *frame,
    int32_t origin_x,int32_t origin_y,uint8_t frame_slot,uint8_t obj_size,
    int16_t pa,int16_t pd,int16_t sample_center_y,int floor_sample)
{
    return render_slot_oam_lod(character,frame,origin_x,origin_y,frame_slot,
        obj_size,pa,pd,sample_center_y,floor_sample,0,0);
}

static int render_slot_oam_double(
    const PortableCharacterAssetView *character,
    PortableSoftwareFrame *frame,
    int32_t origin_x, int32_t origin_y,
    uint8_t frame_slot, int16_t pa, int16_t pd,int physical_origin)
{
    const uint8_t *tiles;
    if(character==NULL||frame==NULL||frame->pixels==NULL||
       character->direction_tiles==NULL||character->palette==NULL||
       character->palette_count<PORTABLE_CHARACTER_PALETTE_COUNT||
       character->direction_tile_bytes<PORTABLE_CHARACTER_FRAME_BYTES||
       frame_slot>=character->direction_frame_count||
       (size_t)frame_slot*PORTABLE_CHARACTER_FRAME_BYTES>
           character->direction_tile_bytes-PORTABLE_CHARACTER_FRAME_BYTES)
        return 0;
    tiles=character->direction_tiles+
        (size_t)frame_slot*PORTABLE_CHARACTER_FRAME_BYTES;
    const int output_scale=(int)portable_software_frame_scale(frame);
    for(int y=0;y<128*output_scale;++y){
        const int32_t target_y=origin_y*(physical_origin?1:output_scale)+y;
        const int sy=32+(int)portable_software_frame_floor_div((y-64*output_scale)*pd,256*output_scale);
        if(target_y<0||target_y>=(int32_t)frame->height*output_scale||sy<0||sy>=64)
            continue;
        for(int x=0;x<128*output_scale;++x){
            const int32_t target_x=origin_x*(physical_origin?1:output_scale)+x;
            const int sx=32+(int)portable_software_frame_floor_div((x-64*output_scale)*pa,256*output_scale);
            size_t offset;
            uint8_t packed,index;
            if(target_x<0||target_x>=(int32_t)frame->width*output_scale||
               sx<0||sx>=64)
                continue;
            offset=((size_t)(sy/8)*8u+(size_t)(sx/8))*32u+
                   (size_t)(sy&7)*4u+(size_t)(sx&7)/2u;
            packed=tiles[offset];
            index=(sx&1)?packed>>4:packed&15u;
            if(index)
                portable_software_frame_store_rgb_physical(frame,(size_t)target_x,(size_t)target_y, color_to_rgb(character->palette[index]));
        }
    }
    return 1;
}

int portable_character_render_slot_oam_double(
    const PortableCharacterAssetView *character,PortableSoftwareFrame *frame,
    int32_t x,int32_t y,uint8_t slot,int16_t pa,int16_t pd)
{
    return render_slot_oam_double(character,frame,x,y,slot,pa,pd,0);
}

int portable_character_render_view_oam_double(
    const PortableCharacterAssetView *character,
    PortableSoftwareFrame *frame,
    int32_t origin_x, int32_t origin_y,
    uint8_t view_angle, int16_t pa, int16_t pd)
{
    size_t table_index;
    if(character==NULL||view_angle>127u||
       character->view_frame_slots==NULL||
       character->view_frame_slot_count<PORTABLE_CHARACTER_VIEW_SLOT_COUNT)
        return 0;
    table_index=view_angle>63u?127u-view_angle:view_angle;
    return portable_character_render_slot_oam_double(character,frame,
        origin_x,origin_y,character->view_frame_slots[table_index],
        pa,pd);
}

static int render_slot_oam_lod_double(
    const PortableCharacterAssetView *character,
    PortableSoftwareFrame *frame,
    int32_t origin_x, int32_t origin_y,
    uint8_t slot, uint8_t obj_size,
    int16_t pa, int16_t pd,int physical_origin)
{
    if(character==NULL||frame==NULL||frame->pixels==NULL||
       character->direction_tiles==NULL||
       (obj_size!=16u&&obj_size!=32u&&obj_size!=64u))
        return 0;
    if(obj_size==64u)
        return render_slot_oam_double(
            character,frame,origin_x,origin_y,slot,pa,pd,physical_origin);
    if(slot>=character->direction_frame_count||
       character->palette==NULL||
       character->palette_count<PORTABLE_CHARACTER_PALETTE_COUNT)
        return 0;
    if(character->direction_tile_bytes<PORTABLE_CHARACTER_FRAME_BYTES ||
       (size_t)slot*PORTABLE_CHARACTER_FRAME_BYTES>character->direction_tile_bytes-PORTABLE_CHARACTER_FRAME_BYTES)return 0;
    const int output_scale=(int)portable_software_frame_scale(frame);
    int texture_size;
    const uint8_t *tiles=character_lod_texture(character,slot,obj_size,output_scale,pa,pd,frame->artwork_lod,&texture_size);
    const int finer_texture=character_lod_texture_changed(character,slot,
        obj_size,output_scale,pa,pd,tiles,texture_size,frame->artwork_lod);
    const int canvas=obj_size*2;
    for(int y=0;y<canvas*output_scale;++y){
        const int sy=obj_size/2+(int)portable_software_frame_floor_div((y-obj_size*output_scale)*pd,256*output_scale);
        const int draw_y=origin_y*(physical_origin?1:output_scale)+y;
        if(sy<0||sy>=obj_size||draw_y<0||
           draw_y>=(int)frame->height*output_scale)continue;
        for(int x=0;x<canvas*output_scale;++x){
            const int sx=obj_size/2+(int)portable_software_frame_floor_div((x-obj_size*output_scale)*pa,256*output_scale);
            const int draw_x=origin_x*(physical_origin?1:output_scale)+x;
            if(sx<0||sx>=obj_size||draw_x<0||
               draw_x>=(int)frame->width*output_scale)continue;
            const int source_x=character_lod_sample(sx,x-obj_size*output_scale,
                pa,obj_size,texture_size,output_scale,1,finer_texture);
            const int source_y=character_lod_sample(sy,y-obj_size*output_scale,
                pd,obj_size,texture_size,output_scale,1,finer_texture);
            if(source_x<0 || source_y<0 || source_x>=texture_size || source_y>=texture_size)continue;
            const size_t offset=((size_t)(source_y/8)*
                (size_t)(texture_size/8)+
                (size_t)(source_x/8))*32u+
                (size_t)(source_y&7)*4u+(size_t)(source_x&7)/2u;
            const uint8_t packed=tiles[offset];
            const uint8_t color_index=(source_x&1)?packed>>4:packed&15u;
            if(color_index)
                portable_software_frame_store_rgb_physical(frame,(size_t)draw_x,(size_t)draw_y, color_to_rgb(character->palette[color_index]));
        }
    }
    return 1;
}

int portable_character_render_view_oam_lod_double(
    const PortableCharacterAssetView *character,PortableSoftwareFrame *frame,
    int32_t x,int32_t y,uint8_t angle,uint8_t obj_size,int16_t pa,int16_t pd)
{
    if(!character || angle>127u || !character->view_frame_slots ||
       character->view_frame_slot_count<64u)return 0;
    return render_slot_oam_lod_double(character,frame,x,y,
        character->view_frame_slots[angle>63u?127u-angle:angle],obj_size,pa,pd,0);
}

/* ECDDC's 64px driver cell is centred14px above the body projection.
 * O3 scales that centre with the inverse Y coefficient. Preserve the native
 * renderer's existing baseline while applying that pivot's relative motion. */
int portable_character_render_driver_view(const PortableCharacterAssetView *character,
    PortableSoftwareFrame *frame,int32_t x,int32_t y,uint8_t angle,
    uint16_t inverse_x,uint16_t inverse_y)
{
    const uint16_t iy=inverse_y?inverse_y:256u;
    const int32_t pivot_delta=14+(-14*256)/(int32_t)iy;
    return portable_character_render_view_scaled(character,frame,x,y+pivot_delta,
        angle,0,inverse_x,iy,0);
}

int portable_character_projection_geometry(const PortableSoftwareFrame *frame,
    const PortableDriverProjectionRules *rules,const PortableCameraProjectionResult *body,
    uint16_t base_inverse_q8,uint16_t effect_x_q8,uint16_t effect_y_q8,int8_t cell_bob,
    PortableCharacterProjectionGeometry *out)
{
    if(!frame||!rules||!body||!out||body->distance<=0)return 0;
    const PortableDriverProjectionRow *row=NULL;
    for(unsigned i=0u;i<PORTABLE_DRIVER_PROJECTION_LODS;++i)
        if((uint16_t)body->distance<=rules->rows[i].max_distance){row=&rules->rows[i];break;}
    if(row==NULL){*out=(PortableCharacterProjectionGeometry){0};return 1;}
    if(row->size!=64u&&row->size!=32u&&row->size!=16u)return 0;
    /* DA18/DA34 divide the base coefficient for LOD before composing +70;
     * 49D78 then multiplies by projected distance, narrowing each halfword. */
    const uint16_t base=(uint16_t)((base_inverse_q8?base_inverse_q8:256u)/(64u/row->size));
    const uint16_t ax=(uint16_t)((uint32_t)base*(effect_x_q8?effect_x_q8:256u)/256u);
    const uint16_t ay=(uint16_t)((uint32_t)base*(effect_y_q8?effect_y_q8:256u)/256u);
    const uint16_t px=(uint16_t)((uint32_t)ax*(uint16_t)body->distance/256u);
    const uint16_t py=(uint16_t)((uint32_t)ay*(uint16_t)body->distance/256u);
    if(px==0u||py==0u||px>INT16_MAX||py>INT16_MAX)return 0;
    const int doubled=row->affine_double_size&&(px<256u||py<256u);
    const int half=row->size/2;
    /* O3's signed-nine-bit X decode is intentionally off by one for
     * negative offsets: -(raw^511), as in the original Thumb. */
    const int32_t decoded_x=row->offset_x+(row->offset_x<0?1:0)+half;
    const int output_scale=(int)portable_software_frame_scale(frame);
    const int32_t center_x=(body->x+row->offset_x+half)*output_scale+
        decoded_x*256*output_scale/(int32_t)px-decoded_x*output_scale;
    const int32_t center_y=body->y*output_scale+((int32_t)row->offset_y+cell_bob+half)*256*output_scale/(int32_t)py;
    const int32_t no_bob=((int32_t)row->offset_y+half)*256*output_scale/(int32_t)py;
    *out=(PortableCharacterProjectionGeometry){.size=row->size,.inverse_x=px,.inverse_y=py,
        .center_x=center_x,.center_y=center_y,
        .bob_delta=center_y-body->y*output_scale-no_bob,.double_size=(uint8_t)doubled};
    return 1;
}

int portable_character_render_driver_pose_projected(const PortableCharacterAssetView *character,
    PortableSoftwareFrame *frame,const PortableDriverProjectionRules *rules,
    const PortableCameraProjectionResult *body,const PortableCharacterViewPose *pose,
    uint16_t base_inverse_q8,uint16_t effect_x_q8,uint16_t effect_y_q8,
    int8_t cell_bob)
{
    if(character==NULL||frame==NULL||pose==NULL||pose->angle>127u||pose->mirror>1u||character->view_frame_slots==NULL||
       character->view_frame_slot_count<64u||rules==NULL||body==NULL||body->distance<=0)return 0;
    PortableCharacterProjectionGeometry geometry;
    if(!portable_character_projection_geometry(frame,rules,body,base_inverse_q8,
        effect_x_q8,effect_y_q8,cell_bob,&geometry))return 0;
    if(!geometry.size)return 1;
    const uint8_t angle=pose->angle;
    const uint8_t slot=pose->frame_override && pose->frame_override<character->direction_frame_count &&
        body->distance<=511 && geometry.size==64u ?
        pose->frame_override : character->view_frame_slots[angle>63u?127u-angle:angle];
    const uint16_t px=geometry.inverse_x,py=geometry.inverse_y;
    const int half=geometry.size/2;
    const int output_scale=(int)portable_software_frame_scale(frame);
    const int32_t x=geometry.center_x-(geometry.double_size?geometry.size:half)*output_scale;
    const int32_t y=geometry.center_y-(geometry.double_size?geometry.size:half)*output_scale;
    if(px==256u&&py==256u){
        return render_slot_oam_lod(character,frame,x,y,slot,(uint8_t)geometry.size,
            256,256,(int16_t)half,1,pose->mirror,1);
    }
    /* Source horizontal mirroring negates PA; affine floor sampling retains
     * its one-pixel edge convention instead of mirroring a rendered bitmap. */
    const int16_t pa=pose->mirror?-(int16_t)px:(int16_t)px;
    if(geometry.double_size)return render_slot_oam_lod_double(
        character,frame,x,y,slot,(uint8_t)geometry.size,pa,(int16_t)py,1);
    return render_slot_oam_lod(character,frame,x,y,slot,
        (uint8_t)geometry.size,pa,(int16_t)py,(int16_t)half,1,0,1);
}

int portable_character_render_driver_projected(const PortableCharacterAssetView *character,
    PortableSoftwareFrame *frame,const PortableDriverProjectionRules *rules,
    const PortableCameraProjectionResult *body,uint8_t angle,
    uint16_t base,uint16_t effect_x,uint16_t effect_y,int8_t bob)
{
    const PortableCharacterViewPose pose={.angle=angle,.mirror=angle>63u};
    return portable_character_render_driver_pose_projected(character,frame,rules,body,&pose,
        base,effect_x,effect_y,bob);
}

int portable_character_render_driver_pose(const PortableCharacterAssetView *character,
    PortableSoftwareFrame *frame,int32_t x,int32_t y,const PortableCharacterViewPose *pose,
    uint16_t inverse_x,uint16_t inverse_y)
{
    if(!character || !pose || pose->angle>127u || pose->mirror>1u ||
       !character->direction_tiles || !character->view_frame_slots ||
       character->view_frame_slot_count<64u || character->direction_tile_bytes<PORTABLE_CHARACTER_FRAME_BYTES)return 0;
    const unsigned angle=pose->angle;
    const uint8_t slot=pose->frame_override && pose->frame_override<character->direction_frame_count ?
        pose->frame_override :
        character->view_frame_slots[angle>63u?127u-angle:angle];
    if(slot>=character->direction_frame_count || (size_t)slot*PORTABLE_CHARACTER_FRAME_BYTES>
       character->direction_tile_bytes-PORTABLE_CHARACTER_FRAME_BYTES)return 0;
    PortableCharacterAssetView selected=*character;
    selected.tiles=character->direction_tiles+(size_t)slot*PORTABLE_CHARACTER_FRAME_BYTES;
    selected.tile_bytes=PORTABLE_CHARACTER_FRAME_BYTES;selected.frame_slot=slot;
    const uint16_t iy=inverse_y?inverse_y:256u;
    const int32_t pivot_delta=14+(-14*256)/(int32_t)iy;
    return render_affine_axes(&selected,frame,x,y+pivot_delta,pose->mirror,0,inverse_x,iy,0);
}
