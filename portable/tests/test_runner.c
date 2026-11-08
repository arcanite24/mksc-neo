#include "test_support.h"

int main(int argc,char **argv)
{
    static const struct { const char *name; int (*run)(void); } cases[]={
        {"--smoke",run_smoke},
        {"--track-y",run_track_y},
        {"--track-import",run_track_import},
        {"--track-object-asset",run_track_object_asset},
        {"--track-object-decoder",run_track_object_decoder},
        {"--track-raster",run_track_raster},
        {"--driver-tick",run_driver_tick},
        {"--driver-pose",run_driver_pose},
        {"--kart-motion",run_kart_motion},
        {"--driver-input",run_driver_input},
        {"--input-script",run_input_script},
        {"--driver-acceleration",run_driver_acceleration},
        {"--driver-forward-acceleration",run_driver_forward_acceleration},
        {"--driver-pilot",run_driver_pilot},
        {"--driver-collision",run_driver_collision},
        {"--terrain-query",run_terrain_query},
        {"--terrain-asset",run_terrain_asset},
        {"--track-object-art",run_track_object_art},
        {"--sky-asset",run_sky_asset},
        {"--sky-motion",run_sky_motion},
        {"--ambient",run_ambient},
        {"--character-view",run_character_view},
        {"--finish-camera",run_finish_camera},
        {"--snow-weather",run_snow_weather},
        {"--course-prop-random",run_course_prop_random},
        {"--character-asset",run_character_asset},
        {"--track-tileset",run_track_tileset},
        {"--road-map",run_road_map},
        {"--road-palette",run_road_palette},
        {"--source-motion-tables",run_source_motion_tables},
        {"--source-b550-tables",run_source_b550_tables},
        {"--road-renderer",run_road_renderer},
        {"--minimap-asset",run_minimap_asset},
        {"--hud-renderer",run_hud_renderer},
        {"--driver-track-state",run_driver_track_state},
        {"--driver-material",run_driver_material},
        {"--driver-track-response",run_driver_track_response},
        {"--driver-terrain-effect",run_driver_terrain_effect},
        {"--driver-vector-effect",run_driver_vector_effect},
        {"--driver-angle-target",run_driver_angle_target},
        {"--driver-angle-callbacks",run_driver_angle_callbacks},
        {"--driver-angle-composition",run_driver_angle_composition},
        {"--terrain-effect-tables",run_terrain_effect_tables},
        {"--collision",run_collision},
        {"--camera",run_camera},
        {"--camera-projection",run_camera_projection},
        {"--camera-affine",run_camera_affine},
    };
    if(argc!=2){fprintf(stderr,"usage: %s TEST-MODE\n",argv[0]);return 2;}
    for(size_t index=0;index<sizeof cases/sizeof cases[0];++index)
        if(strcmp(argv[1],cases[index].name)==0)return cases[index].run();
    if(strcmp(argv[1],"--intentional-failure")==0){
        fputs("synthetic failure: intentional non-zero result\n",stderr);return 7;
    }
    fprintf(stderr,"unknown test mode: %s\n",argv[1]);return 2;
}
