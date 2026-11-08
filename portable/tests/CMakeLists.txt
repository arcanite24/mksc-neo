if(MSVC)
    # Authored fixtures retain many independent race owners in one test frame.
    # This reservation belongs to tests; the player executable uses its default.
    add_link_options(/STACK:8388608)
endif()

add_executable(portable_mesh_palette_scene_tests test_mesh_palette_scene.c)
target_link_libraries(portable_mesh_palette_scene_tests PRIVATE mksc_runtime)
add_test(NAME portable_mesh_palette_scene COMMAND portable_mesh_palette_scene_tests)

add_executable(portable_mesh_sky_tests test_mesh_sky.c)
target_link_libraries(portable_mesh_sky_tests PRIVATE mksc_runtime)
add_test(NAME portable_mesh_sky COMMAND portable_mesh_sky_tests)
add_executable(portable_mesh_palette_tests test_mesh_palette.c)
target_link_libraries(portable_mesh_palette_tests PRIVATE mksc_runtime)
add_test(NAME portable_mesh_palette COMMAND portable_mesh_palette_tests)
add_executable(portable_mesh_sky_racers_tests test_mesh_sky_racers.c)
target_link_libraries(portable_mesh_sky_racers_tests PRIVATE mksc_runtime)
add_test(NAME portable_mesh_sky_racers COMMAND portable_mesh_sky_racers_tests)
add_executable(portable_mesh_sky_progress_tests test_mesh_sky_progress.c)
target_link_libraries(portable_mesh_sky_progress_tests PRIVATE mksc_runtime)
add_test(NAME portable_mesh_sky_progress COMMAND portable_mesh_sky_progress_tests)
add_executable(portable_mesh_sky_scene_tests test_mesh_sky_scene.c)
target_link_libraries(portable_mesh_sky_scene_tests PRIVATE mksc_runtime)
add_test(NAME portable_mesh_sky_scene COMMAND portable_mesh_sky_scene_tests)

foreach(mesh_test IN ITEMS mesh_motion mesh_course_pier_boos mesh_course_kamek mesh_course_mechakoopas mesh_course_common_props mesh_course_thunderclouds mesh_course_falling_stars mesh_course_teepees mesh_course_coins mesh_course_coin_turn mesh_course_actors mesh_course_capture mesh_course_crush mesh_course_strong_hit fixed_3d mesh_query mesh_collision_index mesh_kart mesh_time_trial mesh_time_trial_replay mesh_time_trial_pilot mesh_cup mesh_cup_store mesh_item_rules mesh_item_inventory mesh_item_motion mesh_item_boxes mesh_item_status mesh_item_target mesh_item_art mesh_items mesh_items_state mesh_race mesh_battle mesh_item_scene mesh_item_time_trial mesh_item_replay mesh_item_audio mesh_driver_art mesh_battle_art mesh_battle_pilot)
    add_executable(portable_${mesh_test}_tests test_${mesh_test}.c)
    target_link_libraries(portable_${mesh_test}_tests PRIVATE mksc_runtime)
    if(mesh_test STREQUAL "mesh_item_replay" AND CMAKE_CROSSCOMPILING AND CMAKE_CROSSCOMPILING_EMULATOR)
        add_test(NAME portable_mesh_item_replay COMMAND portable_mesh_item_replay_tests base)
        foreach(families early middle late)
            add_test(NAME portable_mesh_item_replay_${families}
                COMMAND portable_mesh_item_replay_tests ${families})
        endforeach()
    else()
        add_test(NAME portable_${mesh_test} COMMAND portable_${mesh_test}_tests)
    endif()
endforeach()

set(mesh_pilot_work "${CMAKE_CURRENT_BINARY_DIR}/mesh-pilot")
add_test(NAME portable_mesh_pilot_reset COMMAND "${CMAKE_COMMAND}" -E rm -rf "${mesh_pilot_work}")
set_tests_properties(portable_mesh_pilot_reset PROPERTIES FIXTURES_SETUP mesh_pilot_clean)
add_test(NAME portable_mesh_pilot_pack COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../mods/examples/bridge-course/build.py"
    --output-dir "${mesh_pilot_work}/mods")
set_tests_properties(portable_mesh_pilot_pack PROPERTIES
    FIXTURES_REQUIRED mesh_pilot_clean FIXTURES_SETUP mesh_pilot_pack)
add_test(NAME portable_mesh_time_trial_pilot_bridge COMMAND portable_mesh_time_trial_pilot_tests
    "${mesh_pilot_work}/mods/bridge-course/content.pack")
set_tests_properties(portable_mesh_time_trial_pilot_bridge PROPERTIES
    FIXTURES_REQUIRED mesh_pilot_pack TIMEOUT 30)

add_test(NAME portable_mesh_kart_production COMMAND portable_mesh_kart_tests
    "${mesh_pilot_work}/mods/bridge-course/content.pack")
set_tests_properties(portable_mesh_kart_production PROPERTIES
    FIXTURES_REQUIRED mesh_pilot_pack TIMEOUT 30)

add_executable(portable_mesh_kart_rules_tests test_mesh_kart_rules_asset.c)
target_link_libraries(portable_mesh_kart_rules_tests PRIVATE mksc_runtime)
add_test(NAME portable_mesh_kart_rules COMMAND portable_mesh_kart_rules_tests)

add_executable(portable_course_3d_tests test_course_3d.c)
target_link_libraries(portable_course_3d_tests PRIVATE mksc_runtime)
add_test(NAME portable_course_3d COMMAND portable_course_3d_tests)

add_executable(portable_scene_3d_tests test_scene_3d.c)
target_link_libraries(portable_scene_3d_tests PRIVATE mksc_runtime)
add_test(NAME portable_scene_3d COMMAND portable_scene_3d_tests)

add_executable(portable_model_asset_tests test_model_asset.c)
target_link_libraries(portable_model_asset_tests PRIVATE mksc_runtime)
add_test(NAME portable_model_asset COMMAND portable_model_asset_tests)

add_executable(portable_model_renderer_tests test_model_renderer.c)
target_link_libraries(portable_model_renderer_tests PRIVATE mksc_runtime)
add_test(NAME portable_model_renderer COMMAND portable_model_renderer_tests)

add_test(NAME portable_hd2d_shader_sources COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/portable/build_render_shaders.py" --post --verify-sources)

add_test(NAME portable_render_shader_sources COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/portable/build_render_shaders.py" --verify-sources)

add_test(NAME portable_render_shader_tools COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/portable/build_render_shaders.py"
    --self-test --work "${CMAKE_CURRENT_BINARY_DIR}/render-shader-tools")

add_test(NAME portable_ui_shader_sources COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/portable/build_render_shaders.py" --ui --verify-sources)
add_test(NAME portable_ui_shader_tools COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/portable/build_render_shaders.py"
    --ui --self-test --work "${CMAKE_CURRENT_BINARY_DIR}/ui-shader-tools")

if(PORTABLE_BUILD_SDL3_APP)
    # A real video backend permits a GPU device; the test creates no windows.
    set(render_gpu_video offscreen)
    if(APPLE)
        set(render_gpu_video cocoa)
    elseif(WIN32)
        set(render_gpu_video windows)
    elseif(NOT "$ENV{WAYLAND_DISPLAY}" STREQUAL "")
        set(render_gpu_video wayland)
    elseif(NOT "$ENV{DISPLAY}" STREQUAL "")
        set(render_gpu_video x11)
    endif()
    add_executable(portable_render_gles_tests test_render_gpu.c)
    target_compile_definitions(portable_render_gles_tests PRIVATE PORTABLE_TEST_GLES=1)
    target_link_libraries(portable_render_gles_tests PRIVATE mksc_runtime)
    add_test(NAME portable_render_gles COMMAND portable_render_gles_tests)
    set_tests_properties(portable_render_gles PROPERTIES SKIP_RETURN_CODE 77 PORTABLE_GPU_ROUTE TRUE
        ENVIRONMENT "SDL_VIDEO_DRIVER=${render_gpu_video};SDL_AUDIO_DRIVER=dummy;SDL_MAC_BACKGROUND_APP=1;SDL_WINDOW_ACTIVATE_WHEN_SHOWN=0;SDL_WINDOW_ACTIVATE_WHEN_RAISED=0")
    add_test(NAME portable_ui_geometry_gles2 COMMAND portable_render_gles_tests --ui-only)
    set_tests_properties(portable_ui_geometry_gles2 PROPERTIES SKIP_RETURN_CODE 77 PORTABLE_GPU_ROUTE TRUE
        ENVIRONMENT "SDL_VIDEO_DRIVER=${render_gpu_video};SDL_AUDIO_DRIVER=dummy;SDL_MAC_BACKGROUND_APP=1;SDL_WINDOW_ACTIVATE_WHEN_SHOWN=0;SDL_WINDOW_ACTIVATE_WHEN_RAISED=0")
    add_executable(portable_render_gpu_tests test_render_gpu.c)
    target_link_libraries(portable_render_gpu_tests PRIVATE mksc_runtime)
    add_test(NAME portable_render_gpu COMMAND portable_render_gpu_tests)
    set_tests_properties(portable_render_gpu PROPERTIES SKIP_RETURN_CODE 77 PORTABLE_GPU_ROUTE TRUE
        ENVIRONMENT "SDL_VIDEO_DRIVER=${render_gpu_video};SDL_AUDIO_DRIVER=dummy;SDL_MAC_BACKGROUND_APP=1;SDL_WINDOW_ACTIVATE_WHEN_SHOWN=0;SDL_WINDOW_ACTIVATE_WHEN_RAISED=0")
endif()

add_executable(portable_render_scene_tests test_render_scene.c)
target_link_libraries(portable_render_scene_tests PRIVATE mksc_runtime)
add_test(NAME portable_render_scene COMMAND portable_render_scene_tests)

add_executable(portable_render_model_tests test_render_model.c)
target_link_libraries(portable_render_model_tests PRIVATE mksc_runtime)
add_test(NAME portable_render_model COMMAND portable_render_model_tests)

add_executable(portable_menu_driver_model_tests test_menu_driver_model.c)
target_link_libraries(portable_menu_driver_model_tests PRIVATE mksc_runtime)
add_test(NAME portable_menu_driver_model COMMAND portable_menu_driver_model_tests)
set_tests_properties(portable_menu_driver_model PROPERTIES PASS_REGULAR_EXPRESSION "menu driver model: PASS")

add_executable(portable_render_pose_tests test_render_pose.c)
target_link_libraries(portable_render_pose_tests PRIVATE mksc_runtime)
add_test(NAME portable_render_pose COMMAND portable_render_pose_tests)

add_executable(portable_render_chase_tests test_render_chase.c)
target_link_libraries(portable_render_chase_tests PRIVATE mksc_runtime)
add_test(NAME portable_render_chase COMMAND portable_render_chase_tests)

add_executable(portable_render_kart_animation_tests test_render_kart_animation.c)
target_link_libraries(portable_render_kart_animation_tests PRIVATE mksc_runtime)
add_test(NAME portable_render_kart_animation COMMAND portable_render_kart_animation_tests)

add_executable(portable_fx_set_asset_tests test_fx_set_asset.c)
target_link_libraries(portable_fx_set_asset_tests PRIVATE mksc_runtime)
add_test(NAME portable_fx_set_asset COMMAND portable_fx_set_asset_tests)

add_executable(portable_mesh_kart_fx_art_tests test_mesh_kart_fx_art.c)
target_link_libraries(portable_mesh_kart_fx_art_tests PRIVATE mksc_runtime)
add_test(NAME portable_mesh_kart_fx_art COMMAND portable_mesh_kart_fx_art_tests)

add_executable(portable_ui_canvas_alpha_tests test_ui_canvas_alpha.c)
target_link_libraries(portable_ui_canvas_alpha_tests PRIVATE mksc_runtime)
add_test(NAME portable_ui_canvas_alpha COMMAND portable_ui_canvas_alpha_tests)

add_executable(portable_race_depth_tests test_race_depth.c)
target_link_libraries(portable_race_depth_tests PRIVATE mksc_runtime)
add_test(NAME portable_race_depth COMMAND portable_race_depth_tests)

add_executable(portable_race_ground_3d_tests test_race_ground_3d.c)
target_link_libraries(portable_race_ground_3d_tests PRIVATE mksc_runtime)
add_test(NAME portable_race_ground_3d COMMAND portable_race_ground_3d_tests)

add_executable(portable_modern_drift_tests test_modern_drift.c)
target_link_libraries(portable_modern_drift_tests PRIVATE mksc_runtime)
add_test(NAME portable_modern_drift COMMAND portable_modern_drift_tests)

add_executable(portable_driving_profile_tests test_driving_profile.c)
target_link_libraries(portable_driving_profile_tests PRIVATE mksc_runtime)
add_test(NAME portable_driving_profile COMMAND portable_driving_profile_tests)

add_executable(portable_gp_entry_tests test_gp_entry.c)
target_link_libraries(portable_gp_entry_tests PRIVATE mksc_runtime)
# mksc_runtime publishes its math dependency to this fixture.
add_test(NAME portable_gp_entry COMMAND portable_gp_entry_tests)

add_executable(portable_ceremony_motion_tests test_ceremony_motion.c)
target_link_libraries(portable_ceremony_motion_tests PRIVATE mksc_runtime)
add_test(NAME portable_ceremony_motion COMMAND portable_ceremony_motion_tests)

add_executable(portable_test_runner
    test_runner.c
    test_driver_basics.c
    test_driver_pilot.c
    test_driver_terrain.c
    test_content_assets.c
    test_scene_renderers.c
    test_sky_motion.c
    test_ambient.c
    test_character_view.c
    test_finish_camera.c
    test_snow_weather.c
    test_course_prop_random.c
    ../core/track_y.c
    ../core/track_geometry.c
    ../core/driver_tick.c
    ../core/driver_pose.c
    ../core/driver_input.c
    ../core/driver_brake.c
    ../core/driver_acceleration.c
    ../core/driver_forward_acceleration.c
    ../core/driver_forward_limit.c
    ../core/driver_pilot.c ../core/modern_drift.c ../core/modern_air.c
    ../core/driver_hop.c
    ../core/driver_mushroom.c
    ../core/time_trial_ghost.c
    ../core/driver_b550_resultant.c
    ../core/driver_steering.c
    ../core/driver_collision.c
    ../core/driver_material.c
    ../core/driver_track_response.c
    ../core/driver_terrain_effect.c
    ../core/driver_vector_effect.c
    ../core/driver_angle_target.c
    ../core/driver_angle_callbacks.c
    ../core/driver_angle_composition.c
    ../core/driver_track_state.c
    ../core/terrain_query.c
    ../core/kart_motion.c
    ../core/collision.c
    ../core/camera_state.c
    ../core/camera_projection.c
    ../core/camera_affine.c
    ../core/character_camera.c
    ../assets/track_asset.c
    ../assets/native_image.c
    ../assets/track_object_asset.c
    ../assets/track_object_art.c
    ../assets/track_object_decoder.c
    ../assets/track_runtime_map.c
    ../assets/track_raster.c
    ../assets/track_material.c
    ../assets/terrain_asset.c
    ../assets/sky_asset.c
    ../assets/character_asset.c
    ../assets/track_tileset_asset.c
    ../assets/road_palette_asset.c
    ../assets/source_motion_tables.c
    ../assets/source_normal_response_table.c
    ../assets/normal_response.c
    ../assets/source_vector_tables.c
    ../assets/source_b550_tables.c
    ../assets/terrain_effect_tables.c
    ../assets/road_map_asset.c ../assets/road_window.c
    ../assets/minimap_asset.c
    ../assets/hud_asset.c
    ../platform/input_replay.c
    ../platform/input_script.c
    ../platform/sky_renderer.c
    ../platform/character_renderer.c
    ../platform/track_object_renderer.c
    ../platform/road_renderer.c
    ../platform/hud_renderer.c ../app/gp_item_hud.c
    ../platform/software_frame.c ../platform/ui_text.c)
target_compile_features(portable_test_runner PRIVATE c_std_11)
target_include_directories(portable_test_runner PRIVATE ../assets ../core ../platform)

add_test(NAME portable_smoke
         COMMAND portable_test_runner --smoke)
set_tests_properties(portable_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "portable smoke: PASS")

add_executable(portable_driver_hop_tests test_driver_hop.c
    ../core/driver_hop.c)
target_include_directories(portable_driver_hop_tests PRIVATE ../core)
add_test(NAME portable_driver_hop COMMAND portable_driver_hop_tests)

add_executable(portable_driver_mushroom_tests test_driver_mushroom.c
    ../core/driver_mushroom.c)
target_include_directories(portable_driver_mushroom_tests PRIVATE ../core)
add_test(NAME portable_driver_mushroom COMMAND portable_driver_mushroom_tests)

add_executable(portable_gp_race_roster_tests test_gp_race_roster.c ../core/battle_navigation.c
    ../core/driver_hop.c
    ../core/driver_steering.c
    ../core/boo_attack.c ../app/gp_boo.c ../app/gp_item_hud.c ../app/gp_race_roster.c ../core/collision.c ../app/gp_roster_items.c ../app/gp_item_pool.c
    ../app/gp_item_inventory.c ../app/gp_shell_orbit.c ../app/gp_green_shell.c ../app/gp_red_shell.c ../app/gp_banana.c
    ../core/driver_mushroom.c
    ../core/kart_motion.c
    ../core/driver_vector_effect.c ../core/driver_material.c ../core/driver_terrain_effect.c
    ../core/terrain_query.c ../assets/track_raster.c
    ../assets/track_material.c ../assets/terrain_effect_tables.c)
target_include_directories(portable_gp_race_roster_tests PRIVATE
    ../app ../core ../assets)
add_test(NAME portable_gp_race_roster COMMAND portable_gp_race_roster_tests)

add_executable(portable_gp_race_session_tests test_gp_race_session.c ../app/race_audio_timeline.c
    ../app/gp_race_results.c
    ../app/gp_coin_hud.c ../app/gp_rank_hud.c
    ../core/camera_state.c ../core/camera_projection.c
    ../core/character_camera.c
    ../app/gp_race_session.c ../app/course_contacts.c ../platform/diagnostic.c ../core/battle_navigation.c ../app/gp_start_lakitu.c ../app/gp_race_presentation.c
    ../app/gp_item_art_clock.c
    ../app/gp_result_clock.c
    ../app/gp_driving_quality.c
    ../core/boo_attack.c ../app/gp_boo.c ../app/gp_item_hud.c ../app/gp_race_roster.c ../app/gp_roster_items.c ../app/gp_item_pool.c
    ../app/gp_item_inventory.c ../app/gp_shell_orbit.c ../app/gp_green_shell.c
    ../app/gp_red_shell.c ../app/gp_banana.c ../app/race_runtime.c
    ../core/race_session.c ../core/route_geometry.c ../core/driver_pilot.c ../core/modern_drift.c ../core/modern_air.c ../core/driver_angle_composition.c ../core/driver_angle_target.c
    ../core/driver_material.c ../core/driver_terrain_effect.c ../core/driver_pose.c
    ../core/driver_input.c ../core/driver_acceleration.c
    ../core/driver_brake.c ../core/driver_forward_acceleration.c
    ../core/driver_angle_callbacks.c
    ../core/driver_forward_limit.c ../core/driver_steering.c
    ../core/driver_hop.c ../core/driver_mushroom.c
    ../core/water_recovery.c ../core/driver_water_recovery.c
    ../core/kart_motion.c ../core/driver_vector_effect.c
    ../core/driver_b550_resultant.c ../core/collision.c
    ../core/terrain_query.c ../assets/track_raster.c
    ../assets/track_material.c ../assets/source_normal_response_table.c
    ../assets/normal_response.c ../assets/terrain_effect_tables.c)
target_sources(portable_gp_race_session_tests PRIVATE ../platform/script_host.c)
target_link_libraries(portable_gp_race_session_tests PRIVATE portable_lua)
target_include_directories(portable_gp_race_session_tests PRIVATE
    ../app ../core ../assets ../platform ../audio)
if(UNIX)
    target_link_libraries(portable_gp_race_session_tests PRIVATE m)
endif()
# This test holds dozens of full race fixtures in one function: Unix runs it
# on a 64 MiB thread, Windows links a 64 MiB main stack.
if(WIN32)
    if(MSVC)
        target_link_options(portable_gp_race_session_tests PRIVATE /STACK:67108864)
    else()
        target_link_options(portable_gp_race_session_tests PRIVATE -Wl,--stack,67108864)
    endif()
else()
    find_package(Threads REQUIRED)
    target_link_libraries(portable_gp_race_session_tests PRIVATE Threads::Threads)
endif()
add_test(NAME portable_gp_race_session COMMAND portable_gp_race_session_tests)


add_executable(portable_gp_cup_session_tests test_gp_cup_session.c
    ../app/gp_cup_session.c ../app/gp_cup_scoring.c ../app/gp_driving_quality.c)
add_executable(portable_gp_failure_menu_tests test_gp_failure_menu.c
    ../app/gp_failure_menu.c)
target_include_directories(portable_gp_failure_menu_tests PRIVATE ../app)
add_test(NAME portable_gp_failure_menu COMMAND portable_gp_failure_menu_tests)
add_executable(portable_gp_result_clock_tests test_gp_result_clock.c
    ../app/gp_result_clock.c)
target_include_directories(portable_gp_result_clock_tests PRIVATE ../app)
add_test(NAME portable_gp_result_clock COMMAND portable_gp_result_clock_tests)
target_include_directories(portable_gp_cup_session_tests PRIVATE
    ../app ../core ../assets)
add_test(NAME portable_gp_cup_session COMMAND portable_gp_cup_session_tests)

add_executable(portable_gp_driving_quality_tests
    test_gp_driving_quality.c ../app/gp_driving_quality.c)
target_include_directories(portable_gp_driving_quality_tests PRIVATE
    ../app ../core ../assets)
add_test(NAME portable_gp_driving_quality
    COMMAND portable_gp_driving_quality_tests)

add_executable(portable_gp_ceremony_session_tests
    test_gp_ceremony_session.c)
target_link_libraries(portable_gp_ceremony_session_tests PRIVATE mksc_runtime)
target_include_directories(portable_gp_ceremony_session_tests PRIVATE
    ../app ../core ../assets)
add_test(NAME portable_gp_ceremony_session
    COMMAND portable_gp_ceremony_session_tests)

add_executable(portable_gp_progress_store_tests
    test_gp_progress_store.c ../platform/gp_progress_store.c ../platform/mod_identity.c ../core/text_number.c)
target_include_directories(portable_gp_progress_store_tests PRIVATE
    ../platform ../app ../core ../assets)
add_test(NAME portable_gp_progress_store COMMAND portable_gp_progress_store_tests
    "${CMAKE_CURRENT_BINARY_DIR}/portable-gp-progress.bin")
set(gp_progress_reload_path
    "${CMAKE_CURRENT_BINARY_DIR}/portable-gp-progress-reload.bin")
add_test(NAME portable_gp_progress_write
    COMMAND portable_gp_progress_store_tests --write "${gp_progress_reload_path}")
set_tests_properties(portable_gp_progress_write PROPERTIES
    FIXTURES_SETUP portable_gp_progress_reload)
add_test(NAME portable_gp_progress_reload
    COMMAND portable_gp_progress_store_tests --read "${gp_progress_reload_path}")
set_tests_properties(portable_gp_progress_reload PROPERTIES
    FIXTURES_REQUIRED portable_gp_progress_reload)

add_executable(portable_gp_ceremony_renderer_tests test_gp_ceremony_renderer.c)
target_link_libraries(portable_gp_ceremony_renderer_tests PRIVATE mksc_runtime)
target_include_directories(portable_gp_ceremony_renderer_tests PRIVATE
    ../assets ../platform ../app ../core)

add_executable(portable_gp_standings_session_tests
    test_gp_standings_session.c ../app/gp_standings_session.c)
target_include_directories(portable_gp_standings_session_tests PRIVATE
    ../app ../core ../assets)
add_test(NAME portable_gp_standings_session
    COMMAND portable_gp_standings_session_tests)

add_executable(portable_gp_standings_renderer_tests
    test_gp_standings_renderer.c ../platform/gp_standings_renderer.c
    ../platform/software_frame.c ../platform/ui_text.c ../assets/native_image.c)
target_include_directories(portable_gp_standings_renderer_tests PRIVATE
    ../app ../core ../assets ../platform)
add_test(NAME portable_gp_standings_renderer
    COMMAND portable_gp_standings_renderer_tests)

add_executable(portable_input_display_tests
    test_input_display.c ../platform/input_display.c
    ../platform/software_frame.c ../platform/ui_text.c)
target_include_directories(portable_input_display_tests PRIVATE
    ../app ../core ../assets ../platform)
add_test(NAME portable_input_display COMMAND portable_input_display_tests)

add_executable(portable_gp_race_results_tests test_gp_race_results.c
    ../app/gp_race_results.c)
target_include_directories(portable_gp_race_results_tests PRIVATE
    ../app ../core ../assets)
add_test(NAME portable_gp_race_results COMMAND portable_gp_race_results_tests)

add_executable(portable_local_players_tests test_local_players.c ../app/local_players.c)
target_include_directories(portable_local_players_tests PRIVATE ../app)
add_test(NAME portable_local_players COMMAND portable_local_players_tests)

add_executable(portable_gp_results_flow_tests test_gp_results_flow.c
    ../app/gp_results_flow.c)
target_include_directories(portable_gp_results_flow_tests PRIVATE ../app ../core)
add_test(NAME portable_gp_results_flow COMMAND portable_gp_results_flow_tests)

add_executable(portable_gp_results_session_tests test_gp_results_session.c
    ../app/gp_results_session.c ../app/gp_results_flow.c)
target_include_directories(portable_gp_results_session_tests PRIVATE ../app)
add_test(NAME portable_gp_results_session COMMAND portable_gp_results_session_tests)

add_executable(portable_gp_results_renderer_tests test_gp_results_renderer.c)
target_link_libraries(portable_gp_results_renderer_tests PRIVATE mksc_runtime)
add_executable(portable_transformed_resolution_tests test_transformed_resolution.c)
target_link_libraries(portable_transformed_resolution_tests PRIVATE mksc_runtime)
add_test(NAME portable_transformed_resolution COMMAND portable_transformed_resolution_tests)
add_test(NAME portable_subpixel_pan COMMAND portable_transformed_resolution_tests --subpixel-pan)
target_include_directories(portable_gp_results_renderer_tests PRIVATE
    ../app ../assets ../platform)
add_test(NAME portable_gp_results_renderer COMMAND portable_gp_results_renderer_tests)

add_executable(portable_gp_race_hud_tests test_gp_race_hud.c
    ../assets/gp_race_hud_asset.c ../assets/native_image.c ../platform/hud_renderer.c ../app/gp_item_hud.c
    ../platform/software_frame.c ../platform/ui_text.c)
target_include_directories(portable_gp_race_hud_tests PRIVATE
    ../assets ../platform ../core ../app)
add_test(NAME portable_gp_race_hud COMMAND portable_gp_race_hud_tests)

add_executable(portable_item_box_resources_tests
    test_item_box_resources.c ../assets/item_box_asset.c)
target_include_directories(portable_item_box_resources_tests PRIVATE ../assets)
add_test(NAME portable_item_box_resources
         COMMAND portable_item_box_resources_tests)

add_executable(portable_gp_item_art_clock_tests test_gp_item_art_clock.c
    ../app/gp_item_art_clock.c)
target_include_directories(portable_gp_item_art_clock_tests PRIVATE ../app)
add_test(NAME portable_gp_item_art_clock COMMAND portable_gp_item_art_clock_tests)

add_executable(portable_gp_start_lakitu_tests
    test_gp_start_lakitu.c ../app/gp_start_lakitu.c)
target_include_directories(portable_gp_start_lakitu_tests PRIVATE ../app)
add_test(NAME portable_gp_start_lakitu COMMAND portable_gp_start_lakitu_tests)

add_executable(portable_gp_rank_hud_tests test_gp_rank_hud.c ../app/gp_rank_hud.c)
add_executable(portable_gp_coin_hud_tests test_gp_coin_hud.c ../app/gp_coin_hud.c)
target_include_directories(portable_gp_coin_hud_tests PRIVATE ../app)
add_test(NAME portable_gp_coin_hud COMMAND portable_gp_coin_hud_tests)
add_executable(portable_lap_hud_tests test_lap_hud.c)
target_include_directories(portable_lap_hud_tests PRIVATE ../core)
add_test(NAME portable_lap_hud COMMAND portable_lap_hud_tests)
add_executable(portable_gp_finish_portraits_tests test_gp_finish_portraits.c)
target_include_directories(portable_gp_finish_portraits_tests PRIVATE ../core)
add_test(NAME portable_gp_finish_portraits COMMAND portable_gp_finish_portraits_tests)
target_include_directories(portable_gp_rank_hud_tests PRIVATE ../app)
add_test(NAME portable_gp_rank_hud COMMAND portable_gp_rank_hud_tests)
add_executable(portable_gp_turn_cue_tests test_gp_turn_cue.c ../app/gp_turn_cue.c)
target_include_directories(portable_gp_turn_cue_tests PRIVATE ../app)
add_test(NAME portable_gp_turn_cue COMMAND portable_gp_turn_cue_tests)

add_executable(portable_gp_item_inventory_tests test_gp_item_inventory.c
    ../app/gp_item_inventory.c ../app/gp_item_hud.c)
target_include_directories(portable_gp_item_inventory_tests PRIVATE ../app)
add_test(NAME portable_gp_item_inventory COMMAND portable_gp_item_inventory_tests)

add_executable(portable_gp_green_shell_tests test_gp_green_shell.c
    ../app/gp_shell_orbit.c ../app/gp_green_shell.c ../app/gp_item_inventory.c
    ../core/driver_material.c ../core/driver_terrain_effect.c ../core/terrain_query.c
    ../assets/track_raster.c ../assets/track_material.c ../assets/terrain_effect_tables.c)
target_include_directories(portable_gp_green_shell_tests PRIVATE ../app ../core ../assets)
add_test(NAME portable_gp_green_shell COMMAND portable_gp_green_shell_tests)

add_executable(portable_gp_red_shell_tests test_gp_red_shell.c
    ../core/driver_vector_effect.c
    ../app/gp_shell_orbit.c ../app/gp_red_shell.c ../app/gp_item_inventory.c
    ../core/driver_material.c ../core/driver_terrain_effect.c ../core/terrain_query.c
    ../assets/track_raster.c ../assets/track_material.c)
target_include_directories(portable_gp_red_shell_tests PRIVATE ../app ../core ../assets)
add_test(NAME portable_gp_red_shell COMMAND portable_gp_red_shell_tests)

add_executable(portable_gp_banana_tests test_gp_banana.c
    ../app/gp_banana.c ../app/gp_item_inventory.c
    ../core/terrain_query.c ../assets/track_raster.c ../assets/track_material.c)
target_include_directories(portable_gp_banana_tests PRIVATE ../app ../core ../assets)
add_test(NAME portable_gp_banana COMMAND portable_gp_banana_tests)

add_executable(portable_gp_mushroom_tests test_gp_mushroom.c
    ../core/driver_hop.c
    ../core/driver_steering.c
    ../core/boo_attack.c ../app/gp_boo.c ../app/gp_item_hud.c ../app/gp_race_roster.c ../core/collision.c ../app/gp_roster_items.c ../app/gp_item_pool.c ../app/gp_item_inventory.c
    ../app/gp_shell_orbit.c ../app/gp_green_shell.c ../app/gp_red_shell.c ../app/gp_banana.c ../core/driver_mushroom.c
    ../core/kart_motion.c ../core/driver_vector_effect.c
    ../core/driver_material.c ../core/driver_terrain_effect.c ../core/terrain_query.c
    ../assets/track_raster.c ../assets/track_material.c
    ../assets/terrain_effect_tables.c)
target_include_directories(portable_gp_mushroom_tests PRIVATE
    ../app ../core ../assets)
add_test(NAME portable_gp_mushroom COMMAND portable_gp_mushroom_tests)

add_executable(portable_green_shell_renderer_tests
    test_green_shell_renderer.c ../platform/green_shell_renderer.c
    ../platform/software_frame.c ../platform/ui_text.c)
target_include_directories(portable_green_shell_renderer_tests PRIVATE
    ../platform ../assets)
add_test(NAME portable_green_shell_renderer
    COMMAND portable_green_shell_renderer_tests)

add_executable(portable_item_box_renderer_tests
    test_item_box_renderer.c ../platform/item_box_renderer.c
    ../platform/software_frame.c ../platform/ui_text.c)
target_include_directories(portable_item_box_renderer_tests PRIVATE
    ../platform ../assets)
add_test(NAME portable_item_box_renderer
    COMMAND portable_item_box_renderer_tests)

add_executable(portable_lightning_effect_renderer_tests
    test_lightning_effect_renderer.c ../platform/lightning_effect_renderer.c
    ../platform/software_frame.c ../platform/ui_text.c)
target_include_directories(portable_lightning_effect_renderer_tests PRIVATE
    ../platform ../assets)
add_test(NAME portable_lightning_effect_renderer
    COMMAND portable_lightning_effect_renderer_tests)

add_executable(portable_gp_finish_banner_renderer_tests
    test_gp_finish_banner_renderer.c ../platform/gp_finish_banner_renderer.c
    ../platform/software_frame.c ../platform/ui_text.c)
target_include_directories(portable_gp_finish_banner_renderer_tests PRIVATE
    ../platform ../assets)
add_test(NAME portable_gp_finish_banner_renderer
    COMMAND portable_gp_finish_banner_renderer_tests)

add_executable(portable_red_shell_renderer_tests
    test_red_shell_renderer.c ../platform/red_shell_renderer.c
    ../platform/software_frame.c ../platform/ui_text.c)
target_include_directories(portable_red_shell_renderer_tests PRIVATE
    ../assets ../platform)
add_test(NAME portable_red_shell_renderer COMMAND portable_red_shell_renderer_tests)

add_executable(portable_banana_renderer_tests
    test_banana_renderer.c ../platform/banana_renderer.c
    ../platform/software_frame.c ../platform/ui_text.c)
target_include_directories(portable_banana_renderer_tests PRIVATE
    ../assets ../platform)
add_test(NAME portable_banana_renderer
    COMMAND portable_banana_renderer_tests)

add_test(NAME portable_track_y
         COMMAND portable_test_runner --track-y)
set_tests_properties(portable_track_y PROPERTIES
    PASS_REGULAR_EXPRESSION "portable track y: PASS")

add_test(NAME portable_track_import
         COMMAND portable_test_runner --track-import)
set_tests_properties(portable_track_import PROPERTIES
    PASS_REGULAR_EXPRESSION "portable track import: PASS")

add_test(NAME portable_track_object_asset
         COMMAND portable_test_runner --track-object-asset)
set_tests_properties(portable_track_object_asset PROPERTIES
    PASS_REGULAR_EXPRESSION "portable track object asset: PASS")

add_test(NAME portable_track_object_art
         COMMAND portable_test_runner --track-object-art)
set_tests_properties(portable_track_object_art PROPERTIES
    PASS_REGULAR_EXPRESSION "portable track object art: PASS")

add_test(NAME portable_track_object_decoder
         COMMAND portable_test_runner --track-object-decoder)
set_tests_properties(portable_track_object_decoder PROPERTIES
    PASS_REGULAR_EXPRESSION "portable track object decoder: PASS")

add_test(NAME portable_track_raster
         COMMAND portable_test_runner --track-raster)
set_tests_properties(portable_track_raster PROPERTIES
    PASS_REGULAR_EXPRESSION "portable track raster: PASS")

add_test(NAME portable_driver_tick
         COMMAND portable_test_runner --driver-tick)
set_tests_properties(portable_driver_tick PROPERTIES
    PASS_REGULAR_EXPRESSION "portable driver tick: PASS")

add_test(NAME portable_driver_pose
         COMMAND portable_test_runner --driver-pose)
set_tests_properties(portable_driver_pose PROPERTIES
    PASS_REGULAR_EXPRESSION "portable driver pose: PASS")

add_test(NAME portable_kart_motion
         COMMAND portable_test_runner --kart-motion)
set_tests_properties(portable_kart_motion PROPERTIES
    PASS_REGULAR_EXPRESSION "portable kart motion: PASS")

add_test(NAME portable_driver_input
         COMMAND portable_test_runner --driver-input)
set_tests_properties(portable_driver_input PROPERTIES
    PASS_REGULAR_EXPRESSION "portable driver input: PASS")

add_test(NAME portable_input_script
         COMMAND portable_test_runner --input-script)
set_tests_properties(portable_input_script PROPERTIES
    PASS_REGULAR_EXPRESSION "portable input script: PASS")

add_test(NAME portable_driver_acceleration
         COMMAND portable_test_runner --driver-acceleration)
set_tests_properties(portable_driver_acceleration PROPERTIES
    PASS_REGULAR_EXPRESSION "portable driver acceleration: PASS")

add_test(NAME portable_driver_forward_acceleration
         COMMAND portable_test_runner --driver-forward-acceleration)
set_tests_properties(portable_driver_forward_acceleration PROPERTIES
    PASS_REGULAR_EXPRESSION "portable driver forward acceleration: PASS")

add_test(NAME portable_driver_pilot
         COMMAND portable_test_runner --driver-pilot)
set_tests_properties(portable_driver_pilot PROPERTIES
    PASS_REGULAR_EXPRESSION "portable driver pilot: PASS")

add_test(NAME portable_driver_collision
         COMMAND portable_test_runner --driver-collision)
set_tests_properties(portable_driver_collision PROPERTIES
    PASS_REGULAR_EXPRESSION "portable driver collision: PASS")

add_test(NAME portable_terrain_query
         COMMAND portable_test_runner --terrain-query)
set_tests_properties(portable_terrain_query PROPERTIES
    PASS_REGULAR_EXPRESSION "portable terrain query: PASS")

add_test(NAME portable_terrain_asset
         COMMAND portable_test_runner --terrain-asset)
set_tests_properties(portable_terrain_asset PROPERTIES
    PASS_REGULAR_EXPRESSION "portable terrain asset: PASS")

add_test(NAME portable_snow_weather COMMAND portable_test_runner --snow-weather)
set_tests_properties(portable_snow_weather PROPERTIES PASS_REGULAR_EXPRESSION "portable snow weather: PASS")
add_test(NAME portable_course_prop_random COMMAND portable_test_runner --course-prop-random)
add_test(NAME portable_finish_camera COMMAND portable_test_runner --finish-camera)
set_tests_properties(portable_finish_camera PROPERTIES PASS_REGULAR_EXPRESSION "portable finish camera: PASS")

add_test(NAME portable_character_view
         COMMAND portable_test_runner --character-view)
set_tests_properties(portable_character_view PROPERTIES
    PASS_REGULAR_EXPRESSION "portable character view: PASS")

add_test(NAME portable_ambient
         COMMAND portable_test_runner --ambient)
set_tests_properties(portable_ambient PROPERTIES
    PASS_REGULAR_EXPRESSION "portable ambient: PASS")

add_test(NAME portable_sky_motion
         COMMAND portable_test_runner --sky-motion)
set_tests_properties(portable_sky_motion PROPERTIES
    PASS_REGULAR_EXPRESSION "portable sky motion: PASS")

add_test(NAME portable_sky_asset
         COMMAND portable_test_runner --sky-asset)
set_tests_properties(portable_sky_asset PROPERTIES
    PASS_REGULAR_EXPRESSION "portable sky asset: PASS")

add_test(NAME portable_character_asset
         COMMAND portable_test_runner --character-asset)
set_tests_properties(portable_character_asset PROPERTIES
    PASS_REGULAR_EXPRESSION "portable character asset: PASS")

add_test(NAME portable_track_tileset
         COMMAND portable_test_runner --track-tileset)
set_tests_properties(portable_track_tileset PROPERTIES
    PASS_REGULAR_EXPRESSION "portable track tileset: PASS")

add_test(NAME portable_road_map
         COMMAND portable_test_runner --road-map)
set_tests_properties(portable_road_map PROPERTIES
    PASS_REGULAR_EXPRESSION "portable road map: PASS")

add_test(NAME portable_road_palette
         COMMAND portable_test_runner --road-palette)
set_tests_properties(portable_road_palette PROPERTIES
    PASS_REGULAR_EXPRESSION "portable road palette: PASS")

add_test(NAME portable_source_motion_tables
         COMMAND portable_test_runner --source-motion-tables)
set_tests_properties(portable_source_motion_tables PROPERTIES
    PASS_REGULAR_EXPRESSION "portable source motion tables: PASS")

add_test(NAME portable_source_b550_tables
         COMMAND portable_test_runner --source-b550-tables)
set_tests_properties(portable_source_b550_tables PROPERTIES
    PASS_REGULAR_EXPRESSION "portable source b550 tables: PASS")

add_test(NAME portable_road_renderer
         COMMAND portable_test_runner --road-renderer)
set_tests_properties(portable_road_renderer PROPERTIES
    PASS_REGULAR_EXPRESSION "portable road renderer: PASS")

add_test(NAME portable_minimap_asset
         COMMAND portable_test_runner --minimap-asset)
set_tests_properties(portable_minimap_asset PROPERTIES
    PASS_REGULAR_EXPRESSION "portable minimap asset: PASS")

add_test(NAME portable_hud_renderer
         COMMAND portable_test_runner --hud-renderer)
set_tests_properties(portable_hud_renderer PROPERTIES
    PASS_REGULAR_EXPRESSION "portable hud renderer: PASS")

add_test(NAME portable_driver_track_state
         COMMAND portable_test_runner --driver-track-state)
set_tests_properties(portable_driver_track_state PROPERTIES
    PASS_REGULAR_EXPRESSION "portable driver track state: PASS")

add_test(NAME portable_driver_material
         COMMAND portable_test_runner --driver-material)
set_tests_properties(portable_driver_material PROPERTIES
    PASS_REGULAR_EXPRESSION "portable driver material: PASS")

add_test(NAME portable_driver_track_response
         COMMAND portable_test_runner --driver-track-response)
set_tests_properties(portable_driver_track_response PROPERTIES
    PASS_REGULAR_EXPRESSION "portable driver track response: PASS")

add_test(NAME portable_driver_terrain_effect
         COMMAND portable_test_runner --driver-terrain-effect)
set_tests_properties(portable_driver_terrain_effect PROPERTIES
    PASS_REGULAR_EXPRESSION "portable driver terrain effect: PASS")

add_test(NAME portable_driver_vector_effect
         COMMAND portable_test_runner --driver-vector-effect)
set_tests_properties(portable_driver_vector_effect PROPERTIES
    PASS_REGULAR_EXPRESSION "portable driver vector effect: PASS")

add_test(NAME portable_driver_angle_target
         COMMAND portable_test_runner --driver-angle-target)
set_tests_properties(portable_driver_angle_target PROPERTIES
    PASS_REGULAR_EXPRESSION "portable driver angle target: PASS")

add_test(NAME portable_driver_angle_callbacks
         COMMAND portable_test_runner --driver-angle-callbacks)
set_tests_properties(portable_driver_angle_callbacks PROPERTIES
    PASS_REGULAR_EXPRESSION "portable driver angle callbacks: PASS")

add_test(NAME portable_driver_angle_composition
         COMMAND portable_test_runner --driver-angle-composition)
set_tests_properties(portable_driver_angle_composition PROPERTIES
    PASS_REGULAR_EXPRESSION "portable driver angle composition: PASS")

add_test(NAME portable_terrain_effect_tables
         COMMAND portable_test_runner --terrain-effect-tables)
set_tests_properties(portable_terrain_effect_tables PROPERTIES
    PASS_REGULAR_EXPRESSION "portable terrain effect tables: PASS")

add_test(NAME portable_collision
         COMMAND portable_test_runner --collision)
set_tests_properties(portable_collision PROPERTIES
    PASS_REGULAR_EXPRESSION "portable collision: PASS")

add_test(NAME portable_camera
         COMMAND portable_test_runner --camera)
set_tests_properties(portable_camera PROPERTIES
    PASS_REGULAR_EXPRESSION "portable camera: PASS")

add_test(NAME portable_camera_projection
         COMMAND portable_test_runner --camera-projection)
set_tests_properties(portable_camera_projection PROPERTIES
    PASS_REGULAR_EXPRESSION "portable camera projection: PASS")

add_test(NAME portable_camera_affine
         COMMAND portable_test_runner --camera-affine)
set_tests_properties(portable_camera_affine PROPERTIES
    PASS_REGULAR_EXPRESSION "portable camera affine: PASS")

add_executable(portable_character_camera_tests
    test_character_camera.c ../core/character_camera.c)
target_include_directories(portable_character_camera_tests PRIVATE ../core)
add_test(NAME portable_character_camera COMMAND portable_character_camera_tests)

add_executable(portable_charged_exit_particle_tests
    test_charged_exit_particle.c
    ../platform/race_particle_renderer.c
    ../platform/software_frame.c ../platform/ui_text.c)
target_include_directories(portable_charged_exit_particle_tests PRIVATE
    ../assets ../platform)
add_test(NAME portable_charged_exit_particle
         COMMAND portable_charged_exit_particle_tests)

if(PORTABLE_BUILD_SDL3_APP)
    add_executable(portable_file_read_tests test_file_read.c)
    target_link_libraries(portable_file_read_tests PRIVATE mksc_runtime)
    add_executable(portable_file_dialog_tests test_file_dialog.c)
    target_include_directories(portable_file_dialog_tests PRIVATE ../platform)
    target_compile_definitions(portable_file_dialog_tests PRIVATE PORTABLE_ENABLE_SDL3)
    target_link_libraries(portable_file_dialog_tests PRIVATE SDL3::SDL3)
    add_test(NAME portable_file_dialog COMMAND portable_file_dialog_tests)
    set_tests_properties(portable_file_dialog PROPERTIES
        PASS_REGULAR_EXPRESSION "portable file dialog: PASS")
    add_executable(portable_sdl_input_tests test_sdl_input.c)
    target_link_libraries(portable_sdl_input_tests PRIVATE mksc_runtime)
    add_test(NAME portable_sdl_input COMMAND portable_sdl_input_tests)
    add_executable(portable_audio_sdl_smoke
        ../platform/audio_sdl_smoke_main.c
        ../platform/sdl_audio_queue.c
        ../audio/audio_pack_song.c ../audio/audio_song_runtime.c
        ../audio/audio_sequence_asset.c ../audio/audio_sequence_player.c
        ../audio/audio_voice_bank_asset.c ../audio/audio_pcm_mixer.c
        ../audio/audio_psg_mixer.c ../audio/audio_sample_asset.c
        ../audio/audio_session.c ../audio/nds_sequence.c ../audio/n64_sequence.c
        ../assets/asset_pack.c)
    target_compile_features(portable_audio_sdl_smoke PRIVATE c_std_11)
    target_include_directories(portable_audio_sdl_smoke PRIVATE
        ../app ../platform ../audio ../assets)
    target_link_libraries(portable_audio_sdl_smoke PRIVATE SDL3::SDL3)
    if(UNIX)
        target_link_libraries(portable_audio_sdl_smoke PRIVATE m)
    endif()
    add_test(NAME portable_sdl_audio_queue
             COMMAND portable_audio_sdl_smoke --self-test)
    set_tests_properties(portable_sdl_audio_queue PROPERTIES
        PASS_REGULAR_EXPRESSION "portable SDL audio queue: PASS")
endif()


add_test(NAME portable_synthetic_failure_detection
         COMMAND portable_test_runner --intentional-failure)
set_tests_properties(portable_synthetic_failure_detection PROPERTIES
    WILL_FAIL TRUE)

# Target paths differ between configurations in Visual Studio/Xcode builds.
file(GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/verify_synthetic_failure-$<CONFIG>.cmake"
    CONTENT [=[
set(emulator "$<TARGET_PROPERTY:portable_test_runner,CROSSCOMPILING_EMULATOR>")
execute_process(
    COMMAND ${emulator}
            "$<TARGET_FILE:portable_test_runner>" --intentional-failure
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
    TIMEOUT 10)

if(result EQUAL 0)
    message(FATAL_ERROR "intentional-failure mode returned zero")
endif()

string(CONCAT output "${stdout}" "${stderr}")
if(NOT output MATCHES "synthetic failure")
    message(FATAL_ERROR "intentional-failure marker missing")
endif()

message(STATUS "synthetic failure contract: PASS")
]=])

add_test(NAME portable_synthetic_failure_contract
         COMMAND ${CMAKE_COMMAND} -P
                 "${CMAKE_CURRENT_BINARY_DIR}/verify_synthetic_failure-$<CONFIG>.cmake")
set_tests_properties(portable_synthetic_failure_contract PROPERTIES
    PASS_REGULAR_EXPRESSION "synthetic failure contract: PASS")

add_executable(portable_frame_scheduler_tests test_frame_scheduler.c
    ../platform/frame_scheduler.c)
target_include_directories(portable_frame_scheduler_tests PRIVATE ../platform ../core)
add_test(NAME portable_frame_scheduler COMMAND portable_frame_scheduler_tests)

add_executable(portable_controller_input_tests test_controller_input.c
    ../platform/controller_input.c)
target_include_directories(portable_controller_input_tests PRIVATE ../platform ../core)
add_test(NAME portable_controller_input COMMAND portable_controller_input_tests)

add_executable(portable_settings_tests test_settings.c ../platform/settings.c ../platform/json_reader.c ../platform/mod_manifest.c ../platform/controller_input.c)
target_include_directories(portable_settings_tests PRIVATE ../platform ../core)
add_test(NAME portable_settings COMMAND portable_settings_tests)

add_executable(portable_rom_zip_tests test_rom_zip.c)
target_link_libraries(portable_rom_zip_tests PRIVATE mksc_rom_import)
add_test(NAME portable_rom_zip COMMAND portable_rom_zip_tests)
add_executable(portable_mk64_import_tests test_mk64_import.c)
target_link_libraries(portable_mk64_import_tests PRIVATE mksc_runtime)
target_sources(portable_mk64_import_tests PRIVATE ../audio/n64_sequence.c)
target_include_directories(portable_mk64_import_tests PRIVATE ../audio)
add_test(NAME portable_mk64_import COMMAND portable_mk64_import_tests)

add_executable(portable_nds_import_tests test_nds_import.c ../audio/nds_sequence.c)
target_link_libraries(portable_nds_import_tests PRIVATE mksc_runtime)
target_include_directories(portable_nds_import_tests PRIVATE ../audio)
add_test(NAME portable_nds_import COMMAND portable_nds_import_tests)

add_executable(portable_modern_menu_tests test_modern_menu.c)
target_link_libraries(portable_modern_menu_tests PRIVATE mksc_runtime)
add_test(NAME portable_modern_menu COMMAND portable_modern_menu_tests)
add_executable(portable_asset_sources_tests test_asset_sources.c)
target_link_libraries(portable_asset_sources_tests PRIVATE mksc_runtime)
add_test(NAME portable_asset_sources COMMAND portable_asset_sources_tests)

if(PORTABLE_BUILD_SDL3_APP)
    add_executable(portable_mobile_touch_tests test_mobile_touch.c)
    add_executable(portable_mesh_audio_tests test_mesh_audio.c)
    target_link_libraries(portable_mesh_audio_tests PRIVATE mksc_runtime)
    target_link_libraries(portable_mobile_touch_tests PRIVATE mksc_runtime)
    add_test(NAME portable_mobile_touch COMMAND portable_mobile_touch_tests)
    set_tests_properties(portable_mobile_touch PROPERTIES ENVIRONMENT
        "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_MAC_BACKGROUND_APP=1;SDL_WINDOW_ACTIVATE_WHEN_SHOWN=0;SDL_WINDOW_ACTIVATE_WHEN_RAISED=0")
endif()

add_executable(portable_personal_best_tests test_personal_best.c
    ../platform/personal_best.c ../app/time_trial_result.c ../platform/mod_identity.c ../core/text_number.c)
target_include_directories(portable_personal_best_tests PRIVATE ../platform ../core
                           ../app ../assets)
add_test(NAME portable_personal_best COMMAND portable_personal_best_tests)

add_executable(portable_time_trial_ghost_tests test_time_trial_ghost.c
    ../core/time_trial_ghost.c)
target_include_directories(portable_time_trial_ghost_tests PRIVATE ../core)
add_test(NAME portable_time_trial_ghost COMMAND portable_time_trial_ghost_tests)

add_executable(portable_time_trial_ghost_store_tests
    test_time_trial_ghost_store.c ../platform/time_trial_ghost_store.c ../platform/file_read.c ../platform/mod_identity.c ../core/text_number.c
    ../core/time_trial_ghost.c)
target_include_directories(portable_time_trial_ghost_store_tests PRIVATE
    ../platform ../core)
add_test(NAME portable_time_trial_ghost_store
         COMMAND portable_time_trial_ghost_store_tests)
file(GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/verify_ghost_process_reload-$<CONFIG>.cmake"
    CONTENT [=[
set(emulator "$<TARGET_PROPERTY:portable_time_trial_ghost_store_tests,CROSSCOMPILING_EMULATOR>")
set(path "${CMAKE_CURRENT_LIST_DIR}/ghost-process-reload-$<CONFIG>.bin")
file(REMOVE "${path}" "${path}.tmp")
execute_process(
    COMMAND ${emulator}
            "$<TARGET_FILE:portable_time_trial_ghost_store_tests>" --write "${path}"
    RESULT_VARIABLE write_result)
if(NOT write_result EQUAL 0)
    message(FATAL_ERROR "ghost process writer failed")
endif()
execute_process(
    COMMAND ${emulator}
            "$<TARGET_FILE:portable_time_trial_ghost_store_tests>" --read "${path}"
    RESULT_VARIABLE read_result)
file(REMOVE "${path}" "${path}.tmp")
if(NOT read_result EQUAL 0)
    message(FATAL_ERROR "ghost process reader failed")
endif()
message(STATUS "portable ghost process reload: PASS")
]=])
add_test(NAME portable_time_trial_ghost_process_reload
    COMMAND ${CMAKE_COMMAND} -P
            "${CMAKE_CURRENT_BINARY_DIR}/verify_ghost_process_reload-$<CONFIG>.cmake")
set_tests_properties(portable_time_trial_ghost_process_reload PROPERTIES
    PASS_REGULAR_EXPRESSION "portable ghost process reload: PASS")

add_executable(portable_mod_identity_tests test_mod_identity.c ../platform/file_read.c ../platform/mod_identity.c ../core/text_number.c
    ../platform/time_trial_ghost_store.c ../core/time_trial_ghost.c ../platform/personal_best.c
    ../platform/gp_progress_store.c)
target_include_directories(portable_mod_identity_tests PRIVATE ../platform ../core ../app ../assets)
add_test(NAME portable_mod_identity COMMAND portable_mod_identity_tests)

add_executable(portable_time_trial_flow_tests test_time_trial_flow.c
    ../app/time_trial_flow.c)
target_include_directories(portable_time_trial_flow_tests PRIVATE ../app)
add_test(NAME portable_time_trial_flow COMMAND portable_time_trial_flow_tests)

add_executable(portable_frontend_flow_tests test_frontend_flow.c ../app/mesh_cup_menu.c
    ../app/frontend_flow.c ../app/pause_menu.c)
target_include_directories(portable_frontend_flow_tests PRIVATE ../app)
add_test(NAME portable_frontend_flow COMMAND portable_frontend_flow_tests)

add_executable(portable_driver_roster_tests test_driver_roster.c
    ../assets/content_registry.c ../assets/asset_pack.c ../app/frontend_config.c
    ../app/frontend_flow.c ../app/pause_menu.c ../app/frontend_audio.c
    ../audio/audio_command_stream.c ../platform/personal_best.c ../platform/mod_identity.c ../core/text_number.c ../core/time_trial_ghost.c)
target_include_directories(portable_driver_roster_tests PRIVATE ../app ../assets ../audio ../core ../platform)
add_test(NAME portable_driver_roster COMMAND portable_driver_roster_tests
    "${CMAKE_CURRENT_BINARY_DIR}/driver-roster.pb")

add_executable(portable_script_host_tests test_script_host.c)
target_link_libraries(portable_script_host_tests PRIVATE mksc_runtime)
add_test(NAME portable_script_host COMMAND portable_script_host_tests)
set_tests_properties(portable_script_host PROPERTIES PASS_REGULAR_EXPRESSION "script host: PASS")

add_executable(portable_frontend_config_tests test_frontend_config.c)
target_link_libraries(portable_frontend_config_tests PRIVATE mksc_runtime)
add_test(NAME portable_frontend_config COMMAND portable_frontend_config_tests)

add_executable(portable_pause_menu_tests test_pause_menu.c
    ../app/pause_menu.c)
target_include_directories(portable_pause_menu_tests PRIVATE ../app)
add_test(NAME portable_pause_menu COMMAND portable_pause_menu_tests)

add_executable(portable_frontend_input_replay_tests
    test_frontend_input_replay.c ../app/frontend_input_replay.c
    ../app/replay_stream.c ../core/text_number.c ../platform/frontend_replay_file.c
    ../app/frontend_flow.c ../app/pause_menu.c)
target_include_directories(portable_frontend_input_replay_tests PRIVATE ../app)
add_test(NAME portable_frontend_input_replay
         COMMAND portable_frontend_input_replay_tests)

add_executable(portable_frontend_audio_tests
    test_frontend_audio.c ../app/frontend_audio.c ../app/frontend_flow.c
    ../app/pause_menu.c ../audio/audio_command_stream.c)
target_include_directories(portable_frontend_audio_tests PRIVATE ../app ../audio)
add_test(NAME portable_frontend_audio COMMAND portable_frontend_audio_tests)

add_executable(portable_race_audio_timeline_tests
    test_race_audio_timeline.c ../app/race_audio_timeline.c)
target_include_directories(portable_race_audio_timeline_tests PRIVATE
    ../app ../audio ../core)
add_test(NAME portable_race_audio_timeline
         COMMAND portable_race_audio_timeline_tests)

add_executable(portable_engine_control_tests
    test_engine_control.c ../audio/engine_control.c ../assets/engine_sound_asset.c)
target_include_directories(portable_engine_control_tests PRIVATE ../audio ../core ../assets)
target_link_libraries(portable_engine_control_tests PRIVATE mksc_runtime)
add_test(NAME portable_engine_control COMMAND portable_engine_control_tests)

add_executable(portable_audio_command_stream_tests
    test_audio_command_stream.c ../audio/audio_command_stream.c)
target_include_directories(portable_audio_command_stream_tests PRIVATE ../audio)
add_test(NAME portable_audio_command_stream
         COMMAND portable_audio_command_stream_tests)

add_executable(portable_audio_sequence_asset_tests
    test_audio_sequence_asset.c ../audio/audio_sequence_asset.c)
target_include_directories(portable_audio_sequence_asset_tests PRIVATE ../audio)
add_test(NAME portable_audio_sequence_asset
         COMMAND portable_audio_sequence_asset_tests)

add_executable(portable_audio_sequence_player_tests
    test_audio_sequence_player.c ../audio/audio_sequence_asset.c
    ../audio/audio_sequence_player.c)
target_include_directories(portable_audio_sequence_player_tests PRIVATE ../audio)
add_test(NAME portable_audio_sequence_player
         COMMAND portable_audio_sequence_player_tests)

add_executable(portable_audio_sample_asset_tests
    test_audio_sample_asset.c ../audio/audio_sample_asset.c)
target_include_directories(portable_audio_sample_asset_tests PRIVATE ../audio)
add_test(NAME portable_audio_sample_asset
         COMMAND portable_audio_sample_asset_tests)

add_executable(portable_audio_voice_bank_asset_tests
    test_audio_voice_bank_asset.c ../audio/audio_voice_bank_asset.c)
target_include_directories(portable_audio_voice_bank_asset_tests PRIVATE ../audio)
add_test(NAME portable_audio_voice_bank_asset
         COMMAND portable_audio_voice_bank_asset_tests)

add_executable(portable_audio_pcm_mixer_tests
    test_audio_pcm_mixer.c ../audio/audio_pcm_mixer.c
    ../audio/audio_sample_asset.c ../audio/audio_voice_bank_asset.c)
target_include_directories(portable_audio_pcm_mixer_tests PRIVATE ../audio)
add_test(NAME portable_audio_pcm_mixer
         COMMAND portable_audio_pcm_mixer_tests)

add_executable(portable_audio_psg_mixer_tests
    test_audio_psg_mixer.c ../audio/audio_psg_mixer.c
    ../audio/audio_sample_asset.c ../audio/audio_voice_bank_asset.c)
target_include_directories(portable_audio_psg_mixer_tests PRIVATE ../audio)
add_test(NAME portable_audio_psg_mixer
         COMMAND portable_audio_psg_mixer_tests)

add_executable(portable_audio_song_runtime_tests
    test_audio_song_runtime.c ../audio/audio_pack_song.c
    ../audio/audio_song_runtime.c
    ../audio/audio_sequence_asset.c ../audio/audio_sequence_player.c
    ../audio/audio_voice_bank_asset.c ../audio/audio_pcm_mixer.c
    ../audio/audio_psg_mixer.c ../audio/audio_sample_asset.c
    ../assets/asset_pack.c)
target_include_directories(portable_audio_song_runtime_tests PRIVATE
    ../audio ../assets)
add_test(NAME portable_audio_song_runtime
         COMMAND portable_audio_song_runtime_tests)

add_executable(portable_nds_sequence_tests test_nds_sequence.c ../audio/nds_sequence.c)
target_include_directories(portable_nds_sequence_tests PRIVATE ../audio)
add_test(NAME portable_nds_sequence COMMAND portable_nds_sequence_tests)

add_executable(portable_n64_sequence_tests test_n64_sequence.c ../audio/n64_sequence.c)
target_include_directories(portable_n64_sequence_tests PRIVATE ../audio)
if(UNIX)
    target_link_libraries(portable_n64_sequence_tests PRIVATE m)
endif()
add_test(NAME portable_n64_sequence COMMAND portable_n64_sequence_tests)

add_executable(portable_audio_session_tests
    test_audio_session.c ../audio/audio_session.c ../audio/audio_pack_song.c
    ../audio/audio_song_runtime.c ../audio/audio_sequence_asset.c
    ../audio/audio_sequence_player.c ../audio/audio_voice_bank_asset.c
    ../audio/audio_pcm_mixer.c ../audio/audio_psg_mixer.c
    ../audio/audio_sample_asset.c ../audio/nds_sequence.c ../audio/n64_sequence.c ../assets/asset_pack.c ../assets/engine_sound_asset.c)
target_include_directories(portable_audio_session_tests PRIVATE
    ../audio ../assets ../core ../app)
if(UNIX)
    target_link_libraries(portable_audio_session_tests PRIVATE m)
endif()
add_test(NAME portable_audio_session COMMAND portable_audio_session_tests)


add_executable(portable_track_runtime_map_tests test_track_runtime_map.c
    ../assets/track_runtime_map.c ../assets/track_object_asset.c)
target_include_directories(portable_track_runtime_map_tests PRIVATE ../assets)
add_test(NAME portable_track_runtime_map COMMAND portable_track_runtime_map_tests)

add_executable(portable_driver_forward_limit_tests
    test_driver_forward_limit.c
    ../core/driver_forward_limit.c)
target_include_directories(portable_driver_forward_limit_tests PRIVATE ../core)
add_test(NAME portable_driver_forward_limit COMMAND portable_driver_forward_limit_tests)

add_executable(portable_handling_trace_tests test_handling_trace.c
    ../core/handling_trace.c)
target_include_directories(portable_handling_trace_tests PRIVATE ../core)
add_test(NAME portable_handling_trace COMMAND portable_handling_trace_tests)
add_test(NAME portable_handling_trace_failure_detection
    COMMAND portable_handling_trace_tests --intentional-mismatch)
set_tests_properties(portable_handling_trace_failure_detection PROPERTIES
    WILL_FAIL TRUE)

add_executable(portable_race_session_tests test_race_session.c
    ../core/race_session.c ../core/route_geometry.c)
target_include_directories(portable_race_session_tests PRIVATE ../core)
if(UNIX)
    target_link_libraries(portable_race_session_tests PRIVATE m)
endif()
add_test(NAME portable_race_session COMMAND portable_race_session_tests)

add_executable(portable_race_runtime_tests test_race_runtime.c
    ../app/race_runtime.c ../core/race_session.c ../core/route_geometry.c ../core/driver_pilot.c ../core/modern_drift.c ../core/modern_air.c ../core/driver_angle_composition.c ../core/driver_angle_target.c ../core/driver_angle_callbacks.c
    ../core/driver_hop.c
    ../core/driver_mushroom.c
    ../core/driver_material.c ../core/driver_pose.c ../core/driver_input.c
    ../core/driver_acceleration.c ../core/driver_brake.c
    ../core/driver_forward_acceleration.c ../core/driver_forward_limit.c
    ../core/driver_steering.c ../core/kart_motion.c
    ../core/driver_vector_effect.c ../core/driver_b550_resultant.c
    ../core/collision.c ../core/terrain_query.c ../assets/track_raster.c
    ../assets/track_material.c ../assets/source_normal_response_table.c
    ../assets/normal_response.c)
target_include_directories(portable_race_runtime_tests PRIVATE
    ../app ../core ../assets)
if(UNIX)
    target_link_libraries(portable_race_runtime_tests PRIVATE m)
endif()
add_test(NAME portable_race_runtime COMMAND portable_race_runtime_tests)

add_executable(portable_recovered_time_trial_session_tests
    test_recovered_time_trial_session.c
    ../app/time_trial_ghost_playback.c
    ../core/camera_state.c ../core/camera_projection.c
    ../app/recovered_time_trial_session.c ../app/course_contacts.c ../platform/diagnostic.c ../app/race_runtime.c ../core/boo_attack.c
    ../app/gp_start_lakitu.c
    ../core/character_camera.c
    ../core/time_trial_ghost.c
    ../core/race_session.c ../core/route_geometry.c ../core/driver_pilot.c ../core/modern_drift.c ../core/modern_air.c ../core/driver_angle_composition.c ../core/driver_angle_target.c ../core/driver_angle_callbacks.c ../core/driver_material.c
    ../core/driver_hop.c
    ../core/driver_mushroom.c
    ../core/driver_pose.c ../core/driver_input.c ../core/driver_acceleration.c
    ../core/driver_brake.c ../core/driver_forward_acceleration.c
    ../core/driver_forward_limit.c ../core/driver_steering.c
    ../core/kart_motion.c ../core/driver_vector_effect.c
    ../core/driver_b550_resultant.c ../core/collision.c
    ../core/terrain_query.c ../assets/track_raster.c
    ../assets/track_material.c ../assets/source_normal_response_table.c
    ../assets/normal_response.c)
target_include_directories(portable_recovered_time_trial_session_tests PRIVATE
    ../app ../core ../assets)
if(UNIX)
    target_link_libraries(portable_recovered_time_trial_session_tests PRIVATE m)
endif()
add_test(NAME portable_recovered_time_trial_session
    COMMAND portable_recovered_time_trial_session_tests)

add_executable(portable_recovered_time_trial_replay_tests
    test_recovered_time_trial_replay.c
    ../app/recovered_time_trial_replay.c ../app/replay_stream.c ../core/text_number.c ../platform/recovered_replay_file.c
    ../core/camera_state.c ../core/camera_projection.c
    ../app/recovered_time_trial_session.c ../app/course_contacts.c ../platform/diagnostic.c ../app/race_runtime.c ../core/boo_attack.c
    ../app/gp_start_lakitu.c
    ../core/character_camera.c
    ../core/time_trial_ghost.c
    ../core/race_session.c ../core/route_geometry.c ../core/driver_pilot.c ../core/modern_drift.c ../core/modern_air.c ../core/driver_angle_composition.c ../core/driver_angle_target.c ../core/driver_angle_callbacks.c ../core/driver_material.c
    ../core/driver_hop.c
    ../core/driver_mushroom.c
    ../core/driver_pose.c ../core/driver_input.c ../core/driver_acceleration.c
    ../core/driver_brake.c ../core/driver_forward_acceleration.c
    ../core/driver_forward_limit.c ../core/driver_steering.c
    ../core/kart_motion.c ../core/driver_vector_effect.c
    ../core/driver_b550_resultant.c ../core/collision.c
    ../core/terrain_query.c ../assets/track_raster.c
    ../assets/track_material.c ../assets/source_normal_response_table.c
    ../assets/normal_response.c)
target_include_directories(portable_recovered_time_trial_replay_tests PRIVATE
    ../app ../core ../assets)
if(UNIX)
    target_link_libraries(portable_recovered_time_trial_replay_tests PRIVATE m)
endif()
add_test(NAME portable_recovered_time_trial_replay
    COMMAND portable_recovered_time_trial_replay_tests)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(portable_asset_fixture_dir "${CMAKE_CURRENT_BINARY_DIR}/asset-pack-fixture")
file(MAKE_DIRECTORY "${portable_asset_fixture_dir}")
file(WRITE "${portable_asset_fixture_dir}/route.bin" "deterministic route")
file(WRITE "${portable_asset_fixture_dir}/road.bin" "presentation")
file(WRITE "${portable_asset_fixture_dir}/manifest.json"
    "{\"sections\":["
    "{\"kind\":8,\"content_id\":24,\"path\":\"road.bin\"},"
    "{\"kind\":4,\"content_id\":24,\"flags\":3,"
    "\"semantic_revision\":2,\"path\":\"route.bin\"}]}")
execute_process(
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/portable/build_asset_pack.py"
            --manifest "${portable_asset_fixture_dir}/manifest.json"
            --output "${portable_asset_fixture_dir}/fixture.pack"
    RESULT_VARIABLE portable_asset_fixture_result
    OUTPUT_QUIET ERROR_VARIABLE portable_asset_fixture_error)
if(NOT portable_asset_fixture_result EQUAL 0)
    message(FATAL_ERROR "asset pack fixture failed: ${portable_asset_fixture_error}")
endif()
file(WRITE "${portable_asset_fixture_dir}/mod-road.bin" "mod presentation")
file(WRITE "${portable_asset_fixture_dir}/mod-manifest.json"
    "{\"sections\":[{\"kind\":8,\"content_id\":24,\"path\":\"mod-road.bin\"}]}")
execute_process(COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/portable/build_asset_pack.py"
    --manifest "${portable_asset_fixture_dir}/mod-manifest.json"
    --output "${portable_asset_fixture_dir}/mod.pack"
    RESULT_VARIABLE portable_mod_fixture_result OUTPUT_QUIET)
if(NOT portable_mod_fixture_result EQUAL 0)
    message(FATAL_ERROR "mod pack fixture failed")
endif()
add_executable(portable_mod_tests test_mods.c ../assets/asset_sources.c ../platform/mod_loader.c ../assets/model_asset.c ../platform/diagnostic.c ../platform/file_read.c ../platform/source_path.c ../assets/course_3d.c ../assets/mesh_kart_rules_asset.c ../core/mesh_kart_rules.c ../assets/mesh_item_rules_asset.c ../assets/native_image.c ../platform/mod_archive.c ../platform/mod_identity.c ../core/text_number.c ../assets/content_registry.c
    ../platform/mod_manifest.c ../platform/json_reader.c ../platform/game_data.c ../platform/text_data.c ../platform/ui_text.c ../assets/native_image.c ../platform/kart_data.c ../assets/asset_pack.c)
target_include_directories(portable_mod_tests PRIVATE ../platform ../assets ../core)
if(PORTABLE_BUILD_SDL3_APP)
    target_compile_definitions(portable_mod_tests PRIVATE PORTABLE_ENABLE_SDL3=1)
    target_link_libraries(portable_mod_tests PRIVATE SDL3::SDL3)
    add_test(NAME portable_file_read COMMAND portable_file_read_tests
        "${portable_asset_fixture_dir}/zip/mods/zipped.zip")
    add_executable(portable_mod_store_tests test_mod_store.c)
    target_link_libraries(portable_mod_store_tests PRIVATE mksc_runtime)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/store-fixture")
    add_test(NAME portable_mod_store COMMAND portable_mod_store_tests "${CMAKE_CURRENT_BINARY_DIR}/store-fixture")
    add_executable(portable_mod_manager_tests test_mod_manager.c)
    target_link_libraries(portable_mod_manager_tests PRIVATE mksc_runtime)
    add_test(NAME portable_mod_manager COMMAND portable_mod_manager_tests)
endif()
execute_process(COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/portable/tests/make_mod_zip_fixture.py"
    "${portable_asset_fixture_dir}/zip" "${portable_asset_fixture_dir}/mod.pack" COMMAND_ERROR_IS_FATAL ANY)
add_test(NAME portable_mods COMMAND portable_mod_tests
    "${portable_asset_fixture_dir}/fixture.pack" "${portable_asset_fixture_dir}/mod.pack"
    "${portable_asset_fixture_dir}/zip/mods")

set(portable_game_data_fixture_dir "${CMAKE_CURRENT_BINARY_DIR}/game-data-fixture")
execute_process(COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/portable/tests/make_game_data_fixture.py"
    "${portable_game_data_fixture_dir}" COMMAND_ERROR_IS_FATAL ANY)
# The ROM-free example mods build with the modding kit and load as zips.
set(portable_example_mods_dir "${CMAKE_CURRENT_BINARY_DIR}/example-mods")
file(MAKE_DIRECTORY "${portable_example_mods_dir}/zips")
foreach(example data-tuning lucky-start lap-bonus item-swap)
    add_test(NAME portable_example_build_${example} COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/portable/mod_kit.py" build
        "${CMAKE_CURRENT_SOURCE_DIR}/../../mods/examples/${example}" --output "${portable_example_mods_dir}/build")
    set_tests_properties(portable_example_build_${example} PROPERTIES FIXTURES_SETUP portable_example_mods)
endforeach()

add_test(NAME portable_example_zips COMMAND "${CMAKE_COMMAND}" -E copy
    "${portable_example_mods_dir}/build/data-tuning.zip" "${portable_example_mods_dir}/build/lucky-start.zip"
    "${portable_example_mods_dir}/build/lap-bonus.zip" "${portable_example_mods_dir}/build/item-swap.zip"
    "${portable_example_mods_dir}/zips/")
set_tests_properties(portable_example_zips PROPERTIES FIXTURES_SETUP portable_example_mods
    DEPENDS "portable_example_build_data-tuning;portable_example_build_lucky-start;portable_example_build_lap-bonus;portable_example_build_item-swap")
add_executable(portable_mod_example_tests test_mod_examples.c)
target_link_libraries(portable_mod_example_tests PRIVATE mksc_runtime)
add_test(NAME portable_mod_examples COMMAND portable_mod_example_tests
    "${portable_game_data_fixture_dir}/base.pack" "${portable_example_mods_dir}/zips")
set_tests_properties(portable_mod_examples PROPERTIES FIXTURES_REQUIRED portable_example_mods
    PASS_REGULAR_EXPRESSION "mod examples: PASS")
add_executable(portable_game_data_tests test_game_data.c ../platform/ui_text_pack.c ../platform/software_frame.c ../platform/game_data.c ../platform/text_data.c ../platform/ui_text.c ../assets/native_image.c ../platform/kart_data.c
    ../platform/mod_loader.c ../assets/asset_sources.c ../assets/model_asset.c ../platform/diagnostic.c ../platform/file_read.c ../platform/source_path.c ../assets/course_3d.c ../assets/mesh_kart_rules_asset.c ../core/mesh_kart_rules.c ../assets/mesh_item_rules_asset.c ../assets/native_image.c ../platform/mod_archive.c ../platform/mod_identity.c ../core/text_number.c ../platform/mod_manifest.c ../platform/json_reader.c ../assets/asset_pack.c ../assets/content_registry.c)
target_include_directories(portable_game_data_tests PRIVATE ../platform ../assets ../core)
add_test(NAME portable_game_data COMMAND portable_game_data_tests "${portable_game_data_fixture_dir}")

add_executable(portable_asset_pack_tests test_asset_pack.c
    ../assets/asset_pack.c)
target_include_directories(portable_asset_pack_tests PRIVATE ../assets)
add_test(NAME portable_asset_pack COMMAND portable_asset_pack_tests
    "${portable_asset_fixture_dir}/fixture.pack")

set(portable_frontend_fixture_dir
    "${CMAKE_CURRENT_BINARY_DIR}/frontend-pack-fixture")
execute_process(
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/portable/tests/make_frontend_pack_fixture.py"
            "${portable_frontend_fixture_dir}"
    RESULT_VARIABLE portable_frontend_fixture_result
    ERROR_VARIABLE portable_frontend_fixture_error)
if(NOT portable_frontend_fixture_result EQUAL 0)
    message(FATAL_ERROR "frontend pack fixture failed: ${portable_frontend_fixture_error}")
endif()
add_executable(portable_frontend_presentation_tests
    test_frontend_presentation.c ../app/frontend_presentation.c ../app/frontend_flow.c ../app/pause_menu.c
    ../app/frontend_config.c ../assets/content_registry.c
    ../assets/native_image.c ../platform/title_renderer.c ../platform/frontend_image_renderer.c ../platform/menu_chrome_renderer.c ../platform/menu_font.c ../platform/character_menu_renderer.c ../platform/course_menu_renderer.c ../platform/pause_menu_renderer.c
    ../assets/asset_pack.c ../platform/software_frame.c ../platform/ui_text.c)
target_include_directories(portable_frontend_presentation_tests PRIVATE
    ../app ../assets ../platform ../core)
target_link_libraries(portable_frontend_presentation_tests PRIVATE mksc_runtime)
add_test(NAME portable_frontend_presentation
    COMMAND portable_frontend_presentation_tests
            "${portable_frontend_fixture_dir}/fixture.pack")
add_test(NAME portable_gp_ceremony_renderer
    COMMAND portable_gp_ceremony_renderer_tests "${portable_frontend_fixture_dir}/fixture.pack")


set(portable_recovered_fixture_dir "${CMAKE_CURRENT_BINARY_DIR}/recovered-pack-fixture")
set(portable_ceremony_fixture_argument "")
if(PORTABLE_BUILD_SDL3_APP)
    set(portable_ceremony_fixture_argument --ceremony)
endif()
execute_process(
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/portable/tests/make_recovered_pack_fixture.py"
            "${portable_recovered_fixture_dir}" ${portable_ceremony_fixture_argument}
    RESULT_VARIABLE portable_recovered_fixture_result
    ERROR_VARIABLE portable_recovered_fixture_error)
if(NOT portable_recovered_fixture_result EQUAL 0)
    message(FATAL_ERROR "recovered pack fixture failed: ${portable_recovered_fixture_error}")
endif()
add_executable(portable_recovered_pack_tests test_recovered_pack.c ../app/gp_item_inventory.c
    ../assets/asset_pack.c ../assets/recovered_pack.c ../assets/modern_drift_asset.c ../core/modern_drift.c ../core/modern_air.c ../assets/engine_sound_asset.c
    ../assets/presentation_pack.c ../assets/hud_asset.c ../assets/item_box_asset.c ../assets/race_particle_asset.c ../assets/gp_opening_asset.c ../assets/sky_asset.c ../assets/ambient_asset.c ../assets/character_view_asset.c ../assets/finish_asset.c ../assets/weather_asset.c ../assets/recovery_fx_asset.c ../assets/surface_trail_asset.c ../assets/coin_effect_asset.c ../assets/impact_effect_asset.c ../assets/character_asset.c ../assets/driver_projection_asset.c ../assets/native_image.c ../assets/gp_race_hud_asset.c
    ../assets/content_registry.c)
target_include_directories(portable_recovered_pack_tests PRIVATE ../assets ../core ../app)
add_test(NAME portable_recovered_pack COMMAND portable_recovered_pack_tests
    "${portable_recovered_fixture_dir}/fixture.pack"
    "${portable_recovered_fixture_dir}/fixture-v1.pack"
    "${portable_recovered_fixture_dir}/fixture-v4.pack")


# "check_group:NAME" labels select related routes for focused `ctest -L`
# runs; check.py runs the whole suite as one parallel CTest command. Tests
# that share files must share fixtures; parallel tests write distinct saves
# and captures.
function(portable_check_group group)
    set_property(TEST ${ARGN} APPEND PROPERTY LABELS "check_group:${group}")
endfunction()

# Every unattended test must terminate, including platform initialization.
get_property(portable_tests DIRECTORY PROPERTY TESTS)
set_tests_properties(${portable_tests} PROPERTIES TIMEOUT 30)
# Instruction emulation makes these existing pixel loops exceed native budgets.
# Keep assertions and native limits unchanged; only cross-emulator time differs.
if(CMAKE_CROSSCOMPILING AND CMAKE_CROSSCOMPILING_EMULATOR)
    set_tests_properties(portable_race_depth portable_modern_menu portable_frontend_presentation
        PROPERTIES TIMEOUT 180)
endif()

add_executable(portable_source_course_metadata_tests
    test_source_course_metadata.c ../assets/source_course_metadata.c
    ../assets/track_object_asset.c)
target_include_directories(portable_source_course_metadata_tests PRIVATE ../assets)
add_test(NAME portable_source_course_metadata
         COMMAND portable_source_course_metadata_tests)

add_executable(portable_track_asset_select_tests test_track_asset_select.c
    ../assets/track_asset_select.c ../assets/track_asset.c)
target_include_directories(portable_track_asset_select_tests PRIVATE ../assets)
target_compile_features(portable_track_asset_select_tests PRIVATE c_std_11)
add_test(NAME portable_track_asset_select COMMAND portable_track_asset_select_tests)

add_executable(portable_rom_import_tests test_rom_import.c)
target_link_libraries(portable_rom_import_tests PRIVATE mksc_runtime)
add_test(NAME portable_rom_import COMMAND portable_rom_import_tests)

# Owner-content smoke routes. One real-executable route per mode or feature,
# not one per course, backend or setting: pure C tests above carry the rules.
set(portable_sdl_env "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_MAC_BACKGROUND_APP=1;SDL_WINDOW_ACTIVATE_WHEN_SHOWN=0;SDL_WINDOW_ACTIVATE_WHEN_RAISED=0")
set(portable_route_tests "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/portable/tests")
set(PORTABLE_ROM "" CACHE FILEPATH "Owner ROM for native importer checks (read only)")
if(PORTABLE_ROM)
    if(NOT EXISTS "${PORTABLE_ROM}")
        message(FATAL_ERROR "ROM does not exist: ${PORTABLE_ROM}")
    endif()
    add_test(NAME portable_rom_import_matches_reference COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/portable/import_rom.py" --rom "${PORTABLE_ROM}"
        --compare-binary $<TARGET_FILE:mksc>)
    set_tests_properties(portable_rom_import_matches_reference PROPERTIES TIMEOUT 120
        PASS_REGULAR_EXPRESSION "native_import=MATCH")
    if(PORTABLE_BUILD_SDL3_APP)
        # First start: import from the ROM into a fresh data directory, then the shell.
        set(first_run "${CMAKE_CURRENT_BINARY_DIR}/first-run")
        add_test(NAME portable_first_run_reset COMMAND "${CMAKE_COMMAND}" -E rm -rf "${first_run}")
        set_tests_properties(portable_first_run_reset PROPERTIES FIXTURES_SETUP first_run_clean)
        add_test(NAME portable_first_run COMMAND mksc --first-run-smoke "${PORTABLE_ROM}" "${first_run}/saves")
        set_tests_properties(portable_first_run PROPERTIES FIXTURES_REQUIRED first_run_clean TIMEOUT 180
            PASS_REGULAR_EXPRESSION "pack=.*sections=2884.*Game shell: PASS"
            ENVIRONMENT "${portable_sdl_env};MKSC_DATA_DIR=${first_run}/data;MKSC_GP_PROGRESS=${first_run}/gp-progress.bin")
        # A freshly imported pack shared by the menu, mod and 3D routes below.
        set(menu_tour "${CMAKE_CURRENT_BINARY_DIR}/menu-tour")
        add_test(NAME portable_menu_tour_reset COMMAND "${CMAKE_COMMAND}" -E rm -rf "${menu_tour}")
        add_test(NAME portable_menu_tour_directory COMMAND "${CMAKE_COMMAND}" -E make_directory "${menu_tour}")
        add_test(NAME portable_menu_tour_pack COMMAND mksc --import "${PORTABLE_ROM}" "${menu_tour}/pack")
        set_tests_properties(portable_menu_tour_reset PROPERTIES FIXTURES_SETUP menu_tour_reset)
        set_tests_properties(portable_menu_tour_directory PROPERTIES FIXTURES_REQUIRED menu_tour_reset
            FIXTURES_SETUP menu_tour_clean)
        set_tests_properties(portable_menu_tour_pack PROPERTIES FIXTURES_REQUIRED menu_tour_clean
            FIXTURES_SETUP menu_tour_pack TIMEOUT 180 PASS_REGULAR_EXPRESSION "sections=2884")
        # Menu tours press through the shell screens (name:script:final page).
        foreach(tour title:DDDADDB:2 trial:SDEDAEB:3 battle:SUAA.UAADB:5 multi:DADAUAB:3)
            string(REPLACE ":" ";" parts "${tour}")
            list(GET parts 0 name)
            list(GET parts 1 script)
            list(GET parts 2 page)
            string(LENGTH "${script}" presses)
            add_test(NAME portable_menu_tour_${name} COMMAND mksc --menu-tour "${menu_tour}/pack"
                "${menu_tour}/${name}-save" "${script}")
            set_tests_properties(portable_menu_tour_${name} PROPERTIES FIXTURES_REQUIRED menu_tour_pack
                TIMEOUT 120 PASS_REGULAR_EXPRESSION "Menu tour: PASS presses=${presses} .*page=${page}"
                ENVIRONMENT "${portable_sdl_env};MKSC_GP_PROGRESS=${menu_tour}/${name}-gp.bin")
        endforeach()
        add_test(NAME portable_mesh_audio COMMAND portable_mesh_audio_tests "${menu_tour}/pack")
        set_tests_properties(portable_mesh_audio PROPERTIES FIXTURES_REQUIRED menu_tour_pack
            TIMEOUT 30 ENVIRONMENT "SDL_AUDIO_DRIVER=dummy")
        # Python route drivers: one work directory each, cleaned with the pack.
        # name|script|timeout|pass regex|arguments
        foreach(route
                "course_editor_routes|test_course_editor_routes.py|360||"
                "course_3d_route|test_course_3d_routes.py|180|3D course route: PASS|"
                "mod_workflow|test_local_video_routes.py|180|Mod workflow: PASS style=modern|--mods-tab;workflow"
                "store_keyboard|test_store_routes.py|180|Store routes: PASS|--device;keyboard;--video-driver;${render_gpu_video}")
            string(REPLACE "|" ";" parts "${route}")
            list(GET parts 0 name)
            list(GET parts 1 script)
            list(GET parts 2 timeout)
            list(GET parts 3 expected)
            list(SUBLIST parts 4 -1 route_args)
            set(route_work "${menu_tour}/${name}")
            add_test(NAME portable_${name}_reset COMMAND "${CMAKE_COMMAND}" -E rm -rf "${route_work}")
            set_tests_properties(portable_${name}_reset PROPERTIES
                FIXTURES_REQUIRED menu_tour_clean FIXTURES_SETUP ${name}_clean)
            add_test(NAME portable_${name} COMMAND "${Python3_EXECUTABLE}" "${portable_route_tests}/${script}"
                --binary "$<TARGET_FILE:mksc>" --pack "${menu_tour}/pack" --work "${route_work}" ${route_args})
            set_tests_properties(portable_${name} PROPERTIES
                FIXTURES_REQUIRED "menu_tour_pack;${name}_clean" TIMEOUT ${timeout} PORTABLE_GPU_ROUTE TRUE)
            if(expected)
                set_tests_properties(portable_${name} PROPERTIES PASS_REGULAR_EXPRESSION "${expected}")
            endif()
        endforeach()
        # Every GBA course converted to 3D once; one TT, one GP cup and one arena race it.
        set(converted_fixture "${CMAKE_CURRENT_BINARY_DIR}/mesh-converted-fixture")
        add_test(NAME portable_mesh_converted_setup COMMAND "${Python3_EXECUTABLE}"
            "${portable_route_tests}/test_mesh_time_trial_routes.py"
            --binary "$<TARGET_FILE:mksc>" --pack "${menu_tour}/pack"
            --work "${converted_fixture}" --converted-setup)
        set_tests_properties(portable_mesh_converted_setup PROPERTIES
            FIXTURES_REQUIRED menu_tour_pack FIXTURES_SETUP mesh_converted_data TIMEOUT 180 PORTABLE_GPU_ROUTE TRUE
            PASS_REGULAR_EXPRESSION "Converted 3D fixture: PASS courses=40 arenas=4 cups=10 reproducible=1 immutable=1")
        foreach(converted "tt|TT|" "gp|GP|--converted-gp-stage;race;--converted-gp-routes;auto,1,auto,auto;--converted-input-race;1;--converted-contact;cannonballs" "battle|BATTLE|")
            string(REPLACE "|" ";" parts "${converted}")
            list(GET parts 0 converted_mode)
            list(GET parts 1 converted_label)
            list(SUBLIST parts 2 -1 converted_args)
            set(converted_work "${menu_tour}/mesh-converted-${converted_mode}")
            add_test(NAME portable_mesh_converted_${converted_mode}_reset COMMAND "${CMAKE_COMMAND}"
                -E rm -rf "${converted_work}")
            set_tests_properties(portable_mesh_converted_${converted_mode}_reset PROPERTIES
                FIXTURES_REQUIRED menu_tour_clean FIXTURES_SETUP mesh_converted_${converted_mode}_clean)
            add_test(NAME portable_mesh_converted_${converted_mode} COMMAND "${Python3_EXECUTABLE}"
                "${portable_route_tests}/test_mesh_time_trial_routes.py"
                --binary "$<TARGET_FILE:mksc>" --pack "${menu_tour}/pack"
                --work "${converted_work}" --converted-fixture "${converted_fixture}"
                --converted-mode ${converted_mode} --converted-index 0 ${converted_args}
                --gpu-probe "$<TARGET_FILE:portable_render_gpu_tests>" --video-driver "${render_gpu_video}")
            set_tests_properties(portable_mesh_converted_${converted_mode} PROPERTIES
                FIXTURES_REQUIRED "menu_tour_pack;mesh_converted_data;mesh_converted_${converted_mode}_clean"
                TIMEOUT 180 SKIP_RETURN_CODE 77 PORTABLE_GPU_ROUTE TRUE
                PASS_REGULAR_EXPRESSION "Converted 3D ${converted_label} route: PASS index=0")
        endforeach()
        # Installing a mod ZIP through the classic Mods tab, then racing with it.
        set(mod_install "${menu_tour}/mod-install")
        add_test(NAME portable_mod_install_fixture COMMAND "${Python3_EXECUTABLE}"
            "${portable_route_tests}/make_mod_zip_fixture.py" --installer "${mod_install}")
        set_tests_properties(portable_mod_install_fixture PROPERTIES
            FIXTURES_REQUIRED menu_tour_clean FIXTURES_SETUP mod_install_fixture)
        add_test(NAME portable_mod_install_race COMMAND mksc --menu-tour
            "${menu_tour}/pack" "${mod_install}/saves"
            "DDDAEEF...BBBUUUARAAASAAA..." "${mod_install}/frames" --original-transitions --classic-menus)
        set_tests_properties(portable_mod_install_race PROPERTIES
            FIXTURES_REQUIRED "menu_tour_pack;mod_install_fixture" TIMEOUT 120
            PASS_REGULAR_EXPRESSION "mod archive installed: installed 1.0.0.*mod selection applied: count=1.*Game shell Time Trial: PASS"
            ENVIRONMENT "${portable_sdl_env};MKSC_DATA_DIR=${mod_install}/data;MKSC_MENU_TOUR_PERSIST_SETTINGS=1;MKSC_GP_PROGRESS=${mod_install}/progress.bin;MKSC_FRONTEND_RECORD=;MKSC_MENU_TOUR_DROP_FILE=${mod_install}/picked.zip;MKSC_MENU_TOUR_RACES=1")
        # Two-player VS: pause, impact, coins and results boards.
        add_test(NAME portable_race_screens_vs_directory COMMAND "${CMAKE_COMMAND}" -E make_directory "${menu_tour}/race-vs")
        set_tests_properties(portable_race_screens_vs_directory PROPERTIES FIXTURES_REQUIRED menu_tour_clean
            FIXTURES_SETUP race_screens_vs)
        add_test(NAME portable_race_screens_vs COMMAND mksc --vs-smoke "${menu_tour}/pack" 2 24)
        set_tests_properties(portable_race_screens_vs PROPERTIES FIXTURES_REQUIRED "menu_tour_pack;race_screens_vs"
            TIMEOUT 240 PASS_REGULAR_EXPRESSION "Screen capture: vs-pause.*vs-impact.*vs-dropped-coin.*vs-results.*VS local menu: PASS"
            ENVIRONMENT "${portable_sdl_env};MKSC_GP_PROGRESS=${menu_tour}/race-vs-gp.bin;MKSC_SCREEN_DIR=${menu_tour}/race-vs")
    endif()
endif()

if(PORTABLE_BUILD_SDL3_APP)
    set(PORTABLE_IMPORTED_GAME_PACK "" CACHE FILEPATH "Owner-imported pack for the ordinary game shell CTest")
    if(PORTABLE_IMPORTED_GAME_PACK)
        if(NOT EXISTS "${PORTABLE_IMPORTED_GAME_PACK}")
            message(FATAL_ERROR "Imported game pack does not exist: ${PORTABLE_IMPORTED_GAME_PACK}")
        endif()
        if(NOT PORTABLE_ROM)
            add_test(NAME portable_mesh_audio COMMAND portable_mesh_audio_tests "${PORTABLE_IMPORTED_GAME_PACK}")
            set_tests_properties(portable_mesh_audio PROPERTIES TIMEOUT 30 ENVIRONMENT "SDL_AUDIO_DRIVER=dummy")
        endif()
        set(shell_save "${CMAKE_CURRENT_BINARY_DIR}/game-shell-save")
        file(MAKE_DIRECTORY "${shell_save}/screens-shell" "${shell_save}/screens-failure" "${shell_save}/screens-lua")
        # Time Trial and a GP race through the shell, then the saved TT replays.
        add_test(NAME portable_game_shell COMMAND mksc --shell-smoke "${PORTABLE_IMPORTED_GAME_PACK}" "${shell_save}/")
        set_tests_properties(portable_game_shell PROPERTIES TIMEOUT 120 FIXTURES_SETUP finish_follow_run
            PASS_REGULAR_EXPRESSION "Screen capture: tt-countdown.*Time Trial pause graphics: PASS filter=linear internal_scale=2.*Screen capture: tt-finish.*Screen capture: tt-results-menu.*Time Trial pause aid: PASS.*Screen capture: gp-grid.*Screen capture: gp-time-board.*Game shell: PASS"
            ENVIRONMENT "${portable_sdl_env};MKSC_AID_PAUSE_GRAPHICS=1;MKSC_PAUSE_CAPTURE=${shell_save}/screens-shell/tt-pause.ppm;MKSC_TIME_TRIAL_RECORD=${shell_save}/finish-input.txt;MKSC_GP_PROGRESS=${shell_save}/gp-progress.bin;MKSC_SCREEN_DIR=${shell_save}/screens-shell")
        add_test(NAME portable_finish_follow_replay COMMAND mksc --time-trial-finished-replay
            "${PORTABLE_IMPORTED_GAME_PACK}" "${shell_save}/finish-input.txt" 24 1)
        set_tests_properties(portable_finish_follow_replay PROPERTIES FIXTURES_REQUIRED finish_follow_run TIMEOUT 30
            PASS_REGULAR_EXPRESSION "recovered Time Trial replay: PASS .*laps=3/3")
        # A whole Mushroom Cup that loses the final race four times: retry and Game Over.
        add_test(NAME portable_gp_cup_fixture COMMAND "${Python3_EXECUTABLE}"
            "${portable_route_tests}/make_gp_cup_fixture.py" "${shell_save}/cup-routes")
        set_tests_properties(portable_gp_cup_fixture PROPERTIES FIXTURES_SETUP gp_cup_saves TIMEOUT 30)
        add_test(NAME portable_gp_cup COMMAND mksc --gp-cup-smoke "${PORTABLE_IMPORTED_GAME_PACK}" 0 1 0)
        set_tests_properties(portable_gp_cup PROPERTIES FIXTURES_REQUIRED gp_cup_saves TIMEOUT 240
            PASS_REGULAR_EXPRESSION "GP failed result: rank=4 credits=3 .*GP retry window: credits=3 .*Screen capture: gp-game-over.*GP menu cup: PASS .*end=game-over"
            ENVIRONMENT "${portable_sdl_env};MKSC_GP_PROGRESS=${shell_save}/cup-routes/ordinary-0-0.bin;MKSC_SCREEN_DIR=${shell_save}/screens-failure")
        add_test(NAME portable_imported_arenas COMMAND portable_recovered_pack_tests
            --arenas "${PORTABLE_IMPORTED_GAME_PACK}")
        set_tests_properties(portable_imported_arenas PROPERTIES TIMEOUT 30)
        add_test(NAME portable_battle_menu COMMAND mksc --battle-menu-smoke "${PORTABLE_IMPORTED_GAME_PACK}" 0 1)
        set_tests_properties(portable_battle_menu PROPERTIES TIMEOUT 120
            ENVIRONMENT "${portable_sdl_env};MKSC_GP_PROGRESS=${shell_save}/battle-progress.bin")
        add_test(NAME portable_quick_run_shell COMMAND mksc --quick-run-menu-smoke "${PORTABLE_IMPORTED_GAME_PACK}" 0 1 2)
        set_tests_properties(portable_quick_run_shell PROPERTIES TIMEOUT 120
            ENVIRONMENT "${portable_sdl_env};MKSC_GP_PROGRESS=${shell_save}/quick-progress.bin")
        # Modding: a data-tuning pack mod and the Lua examples in a real race.
        add_test(NAME portable_data_mod_build COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../mods/examples/data-tuning/build.py"
            "${PORTABLE_IMPORTED_GAME_PACK}" --output-dir "${shell_save}/mods/data-tuning")
        set_tests_properties(portable_data_mod_build PROPERTIES FIXTURES_SETUP data_mod TIMEOUT 30)
        add_test(NAME portable_data_mod_quick_run COMMAND mksc --quick-run-replay-smoke "${PORTABLE_IMPORTED_GAME_PACK}" 0 1 2
            --mods-dir "${shell_save}/mods" --mod data-tuning)
        set_tests_properties(portable_data_mod_quick_run PROPERTIES FIXTURES_REQUIRED data_mod TIMEOUT 120
            PASS_REGULAR_EXPRESSION "Quick Run replay: PASS"
            ENVIRONMENT "${portable_sdl_env};MKSC_GP_PROGRESS=${shell_save}/data-quick-progress.bin")
        set(portable_lua_mods --mods-dir "${CMAKE_CURRENT_SOURCE_DIR}/../../mods/examples"
            --mod lucky-start --mod lap-bonus --mod item-swap)
        add_test(NAME portable_lua_mods_gp COMMAND mksc --gp-cup-smoke "${PORTABLE_IMPORTED_GAME_PACK}" 0 1 0 ${portable_lua_mods})
        set_tests_properties(portable_lua_mods_gp PROPERTIES TIMEOUT 300
            PASS_REGULAR_EXPRESSION "Screen capture: script-message.*lucky-start\\] race start \\(gp.*final lap mushroom.*green_shell -> banana.*GP menu cup: PASS"
            ENVIRONMENT "${portable_sdl_env};MKSC_GP_PROGRESS=${shell_save}/lua-gp-progress.bin;MKSC_SCREEN_DIR=${shell_save}/screens-lua")
        add_test(NAME portable_lua_local_battle COMMAND mksc --battle-smoke
            "${PORTABLE_IMPORTED_GAME_PACK}" 2 0 ${portable_lua_mods})
        set_tests_properties(portable_lua_local_battle PROPERTIES TIMEOUT 120
            PASS_REGULAR_EXPRESSION "Screen capture: battle-local-script.*local item users: 12.*local menu: PASS"
            FAIL_REGULAR_EXPRESSION "script disabled;Local race failed"
            ENVIRONMENT "${portable_sdl_env};MKSC_GP_PROGRESS=${shell_save}/lua-local-battle-progress.bin;MKSC_SCREEN_DIR=${shell_save}/screens-lua")
    endif()
    add_executable(portable_gp_ceremony_host_tests test_gp_ceremony_host.c)
    target_link_libraries(portable_gp_ceremony_host_tests PRIVATE mksc_runtime)
    add_test(NAME portable_gp_ceremony_host COMMAND portable_gp_ceremony_host_tests
        "${portable_recovered_fixture_dir}/ceremony-fixture.pack")
    set_tests_properties(portable_gp_ceremony_host PROPERTIES TIMEOUT 30
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_MAC_BACKGROUND_APP=1;SDL_WINDOW_ACTIVATE_WHEN_SHOWN=0;SDL_WINDOW_ACTIVATE_WHEN_RAISED=0")
    add_executable(portable_gp_results_host_tests test_gp_results_host.c)
    target_link_libraries(portable_gp_results_host_tests PRIVATE mksc_runtime)
    add_test(NAME portable_gp_results_host COMMAND portable_gp_results_host_tests
        "${portable_frontend_fixture_dir}/fixture.pack")
    set_tests_properties(portable_gp_results_host PROPERTIES TIMEOUT 30
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_MAC_BACKGROUND_APP=1;SDL_WINDOW_ACTIVATE_WHEN_SHOWN=0;SDL_WINDOW_ACTIVATE_WHEN_RAISED=0")
    add_executable(portable_gp_host_tests test_gp_host.c)
    target_link_libraries(portable_gp_host_tests PRIVATE mksc_runtime)
    if(PORTABLE_IMPORTED_GAME_PACK)
        add_test(NAME portable_imported_visual_seeds COMMAND portable_gp_host_tests
            --visual-seeds "${PORTABLE_IMPORTED_GAME_PACK}")
        set_tests_properties(portable_imported_visual_seeds PROPERTIES TIMEOUT 30
            PASS_REGULAR_EXPRESSION "imported visual seeds: PASS eleven placed Boos")
        add_test(NAME portable_imported_arena_sessions COMMAND portable_gp_host_tests
            --arena-sessions "${PORTABLE_IMPORTED_GAME_PACK}")
    endif()
    add_test(NAME portable_gp_host COMMAND portable_gp_host_tests
        "${portable_recovered_fixture_dir}/gp-fixture.pack")
    # Eight internal scales up to 8x; MSVC debug builds need about 25 s.
    set_tests_properties(portable_gp_host PROPERTIES TIMEOUT 60
        FAIL_REGULAR_EXPRESSION "3D unavailable"
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_MAC_BACKGROUND_APP=1;SDL_WINDOW_ACTIVATE_WHEN_SHOWN=0;SDL_WINDOW_ACTIVATE_WHEN_RAISED=0")
    add_executable(portable_time_trial_host_tests test_time_trial_host.c)
    target_link_libraries(portable_time_trial_host_tests PRIVATE mksc_runtime)
    add_test(NAME portable_time_trial_host
        COMMAND portable_time_trial_host_tests
            "${portable_recovered_fixture_dir}/fixture.pack"
            "${portable_frontend_fixture_dir}/fixture.pack")
    set_tests_properties(portable_time_trial_host PROPERTIES TIMEOUT 30
        FAIL_REGULAR_EXPRESSION "3D unavailable"
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_MAC_BACKGROUND_APP=1;SDL_WINDOW_ACTIVATE_WHEN_SHOWN=0;SDL_WINDOW_ACTIVATE_WHEN_RAISED=0")
endif()

add_executable(portable_gp_countdown_renderer_tests test_gp_countdown_renderer.c)
target_link_libraries(portable_gp_countdown_renderer_tests PRIVATE mksc_runtime)
add_test(NAME portable_gp_countdown_renderer COMMAND portable_gp_countdown_renderer_tests)

add_executable(portable_lava_bubble_tests test_lava_bubble.c)
target_include_directories(portable_lava_bubble_tests PRIVATE ../core)
add_test(NAME portable_lava_bubble COMMAND portable_lava_bubble_tests)
set_tests_properties(portable_lava_bubble PROPERTIES TIMEOUT 30)

add_executable(portable_thwomp_tests test_thwomp.c)
target_include_directories(portable_thwomp_tests PRIVATE ../core)
add_test(NAME portable_thwomp COMMAND portable_thwomp_tests)
set_tests_properties(portable_thwomp PROPERTIES TIMEOUT 30)

add_executable(portable_crab_tests test_crab.c)
target_include_directories(portable_crab_tests PRIVATE ../core)
add_test(NAME portable_crab COMMAND portable_crab_tests)
set_tests_properties(portable_crab PROPERTIES TIMEOUT 30)

add_executable(portable_seagull_tests test_seagull.c)
target_include_directories(portable_seagull_tests PRIVATE ../core)
add_test(NAME portable_seagull COMMAND portable_seagull_tests)
set_tests_properties(portable_seagull PROPERTIES TIMEOUT 30)
add_executable(portable_cannonball_tests test_cannonball.c)
target_include_directories(portable_cannonball_tests PRIVATE ../core)
add_test(NAME portable_cannonball COMMAND portable_cannonball_tests)
set_tests_properties(portable_cannonball PROPERTIES TIMEOUT 30)

add_executable(portable_piranha_tests test_piranha.c)
target_include_directories(portable_piranha_tests PRIVATE ../core)
add_test(NAME portable_piranha COMMAND portable_piranha_tests)
set_tests_properties(portable_piranha PROPERTIES TIMEOUT 30)

add_executable(portable_snowman_tests test_snowman.c)
target_include_directories(portable_snowman_tests PRIVATE ../core)
add_test(NAME portable_snowman COMMAND portable_snowman_tests)
set_tests_properties(portable_snowman PROPERTIES TIMEOUT 30)

add_executable(portable_penguin_tests test_penguin.c)
target_include_directories(portable_penguin_tests PRIVATE ../core)
add_test(NAME portable_penguin COMMAND portable_penguin_tests)
set_tests_properties(portable_penguin PROPERTIES TIMEOUT 30)

add_executable(portable_boo_tests test_boo.c)
target_include_directories(portable_boo_tests PRIVATE ../core)
add_test(NAME portable_boo COMMAND portable_boo_tests)
set_tests_properties(portable_boo PROPERTIES TIMEOUT 30)

add_executable(portable_boo_attack_renderer_tests
    test_boo_attack_renderer.c ../platform/boo_attack_renderer.c)
target_include_directories(portable_boo_attack_renderer_tests PRIVATE ../assets ../platform)
add_test(NAME portable_boo_attack_renderer COMMAND portable_boo_attack_renderer_tests)

add_executable(portable_time_trial_records_tests test_time_trial_records.c)
target_link_libraries(portable_time_trial_records_tests PRIVATE mksc_runtime)
add_test(NAME portable_time_trial_records COMMAND portable_time_trial_records_tests)

add_executable(portable_time_trial_ghost_library_tests test_time_trial_ghost_library.c)
target_link_libraries(portable_time_trial_ghost_library_tests PRIVATE mksc_runtime)
add_test(NAME portable_time_trial_ghost_library COMMAND portable_time_trial_ghost_library_tests)

add_executable(portable_time_trial_ghost_exchange_tests test_time_trial_ghost_exchange.c)
target_link_libraries(portable_time_trial_ghost_exchange_tests PRIVATE mksc_runtime)
add_test(NAME portable_time_trial_ghost_exchange COMMAND portable_time_trial_ghost_exchange_tests
    "${portable_recovered_fixture_dir}/fixture.pack")
set_tests_properties(portable_time_trial_ghost_exchange PROPERTIES TIMEOUT 30)

add_executable(portable_race_interpolation_tests test_race_interpolation.c
    ../platform/race_interpolation.c ../platform/frame_scheduler.c)
target_include_directories(portable_race_interpolation_tests PRIVATE ../platform ../core ../app ../assets)
add_test(NAME portable_race_interpolation COMMAND portable_race_interpolation_tests)

add_executable(portable_race_rumble_tests test_race_rumble.c ../app/race_rumble.c)
target_include_directories(portable_race_rumble_tests PRIVATE ../app ../core ../assets)
target_link_libraries(portable_race_rumble_tests PRIVATE mksc_runtime)
add_test(NAME portable_race_rumble COMMAND portable_race_rumble_tests)

add_executable(portable_course_mirror_tests test_course_mirror.c)
target_include_directories(portable_course_mirror_tests PRIVATE ../assets ../core ../app)
target_link_libraries(portable_course_mirror_tests PRIVATE mksc_runtime)
add_test(NAME portable_course_mirror COMMAND portable_course_mirror_tests
    "${portable_recovered_fixture_dir}/fixture.pack")

target_sources(portable_gp_race_hud_tests PRIVATE
    ../assets/gp_opening_asset.c ../platform/gp_opening_renderer.c ../app/gp_opening_veil.c)

# Standalone simulation/content fixtures share the same actor implementation
# as the shipped runtime; other host tests inherit it through mksc_runtime.
foreach(actor_test portable_test_runner portable_gp_race_session_tests portable_gp_race_roster_tests
        portable_race_runtime_tests portable_gp_mushroom_tests
        portable_recovered_time_trial_session_tests portable_recovered_time_trial_replay_tests
        portable_recovered_pack_tests)
    target_link_libraries(${actor_test} PRIVATE mksc_world_actors)
endforeach()

# Real menu/race tours have outgrown the default batch when both owner-ROM
# and imported-pack coverage are enabled. Keep every test and the fixed
# command budget; run each existing application flow as its own bounded group.
get_property(portable_flow_tests DIRECTORY PROPERTY TESTS)
foreach(flow_test IN LISTS portable_flow_tests)
    if(flow_test MATCHES "^portable_(mod_manager|driving_menu|race_screens|lua_local)_")
        set(flow_group "${CMAKE_MATCH_1}")
        get_test_property(${flow_test} LABELS flow_labels)
        if(NOT flow_labels MATCHES "check_group:")
            portable_check_group(${flow_group} ${flow_test})
        endif()
    endif()
endforeach()

# Owner Mario Kart DS (US) cartridge: the importer in process, the CLI into a
# fresh mods folder, and with SDL and the owner GBA ROM, a Mods tab drop that
# is enabled through ordinary input.
set(PORTABLE_MKDS_ROM "" CACHE FILEPATH "Owner Mario Kart DS (US) ROM for game import checks (read only)")
if(PORTABLE_MKDS_ROM)
    if(NOT EXISTS "${PORTABLE_MKDS_ROM}")
        message(FATAL_ERROR "ROM does not exist: ${PORTABLE_MKDS_ROM}")
    endif()
    add_test(NAME portable_mkds_import COMMAND portable_nds_import_tests --mkds "${PORTABLE_MKDS_ROM}")
    set_tests_properties(portable_mkds_import PROPERTIES TIMEOUT 120
        PASS_REGULAR_EXPRESSION "mkds import: PASS.*nds import: PASS")
    set(mkds_import "${CMAKE_CURRENT_BINARY_DIR}/mkds-import")
    add_test(NAME portable_mkds_import_reset COMMAND "${CMAKE_COMMAND}" -E rm -rf "${mkds_import}")
    set_tests_properties(portable_mkds_import_reset PROPERTIES FIXTURES_SETUP mkds_import_clean)
    add_test(NAME portable_mkds_import_cli COMMAND mksc --import-game "${PORTABLE_MKDS_ROM}" "${mkds_import}/cli/mods")
    set_tests_properties(portable_mkds_import_cli PROPERTIES FIXTURES_REQUIRED mkds_import_clean TIMEOUT 120
        PASS_REGULAR_EXPRESSION "Imported Mario Kart DS: 8 cups, 32 courses, 6 battle stages, 13 drivers, 37 karts.*mod=.*mario-kart-ds pack_sha256=")
    if(PORTABLE_BUILD_SDL3_APP AND PORTABLE_ROM)
        set(mkds_environment "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_MAC_BACKGROUND_APP=1;SDL_WINDOW_ACTIVATE_WHEN_SHOWN=0;SDL_WINDOW_ACTIVATE_WHEN_RAISED=0;MKSC_DATA_DIR=${mkds_import}/data;MKSC_MENU_TOUR_PERSIST_SETTINGS=1;MKSC_GP_PROGRESS=${mkds_import}/progress.bin;MKSC_FRONTEND_RECORD=")
        add_test(NAME portable_mkds_import_drop COMMAND mksc --menu-tour "${menu_tour}/pack" "${mkds_import}/saves"
            "DDDAEEF..." "${mkds_import}/drop-frames" --original-transitions --classic-menus)
        set_tests_properties(portable_mkds_import_drop PROPERTIES
            FIXTURES_REQUIRED "menu_tour_pack;mkds_import_clean" TIMEOUT 180
            PASS_REGULAR_EXPRESSION "game import finished: Imported Mario Kart DS.*mod loaded: mario-kart-ds 1.0.[0-9]+.*mod selection applied: count=1.*Menu tour: PASS"
            ENVIRONMENT "${mkds_environment};MKSC_MENU_TOUR_DROP_FILE=${PORTABLE_MKDS_ROM}")
    endif()
endif()

# Modern Options > Games: navigation, staged asset sources, enabling an
# imported Mario Kart 64 card and, with owner ROMs, imports from the cards.
if(PORTABLE_BUILD_SDL3_APP AND DEFINED menu_tour)
    foreach(games_device keyboard)
        set(games_work "${menu_tour}/games-${games_device}")
        add_test(NAME portable_game_library_${games_device}_reset COMMAND "${CMAKE_COMMAND}"
            -E rm -rf "${games_work}")
        set_tests_properties(portable_game_library_${games_device}_reset PROPERTIES
            FIXTURES_REQUIRED menu_tour_clean FIXTURES_SETUP games_${games_device}_clean)
        set(games_rom_args --mk64-fixture "$<TARGET_FILE:portable_mk64_import_tests>")
        if(PORTABLE_MKDS_ROM AND games_device STREQUAL "keyboard")
            list(APPEND games_rom_args --rom "${PORTABLE_MKDS_ROM}")
        endif()
        if(PORTABLE_MK64_ROM AND games_device STREQUAL "keyboard")
            list(APPEND games_rom_args --mk64-rom "${PORTABLE_MK64_ROM}")
        endif()
        add_test(NAME portable_game_library_${games_device} COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/portable/tests/test_game_library_routes.py"
            --binary "$<TARGET_FILE:mksc>" --pack "${menu_tour}/pack" --work "${games_work}" --device ${games_device} ${games_rom_args})
        set_tests_properties(portable_game_library_${games_device} PROPERTIES
            FIXTURES_REQUIRED "menu_tour_pack;games_${games_device}_clean" TIMEOUT 600
            PASS_REGULAR_EXPRESSION "Imported games: PASS device=${games_device}")
    endforeach()
endif()

# PORTABLE_GPU_ROUTE marks a real-device GPU/GLES route. Instead of running
# them serially, they share check.py's "gpu" resource pool (a few parallel
# device slots). Parallel load stretches wall time, so long routes keep a
# larger hang guard.
get_property(portable_all_tests DIRECTORY PROPERTY TESTS)
foreach(portable_test IN LISTS portable_all_tests)
    get_test_property(${portable_test} PORTABLE_GPU_ROUTE portable_gpu_route)
    if(portable_gpu_route)
        set_tests_properties(${portable_test} PROPERTIES RESOURCE_GROUPS "gpu:1")
    endif()
    get_test_property(${portable_test} TIMEOUT portable_timeout)
    if(portable_timeout GREATER_EQUAL 150)
        math(EXPR portable_timeout "${portable_timeout} * 3")
        set_tests_properties(${portable_test} PROPERTIES TIMEOUT ${portable_timeout})
    endif()
endforeach()

if(PORTABLE_BUILD_SDL3_APP AND NOT EMSCRIPTEN)
    add_executable(portable_load_job_tests test_load_job.c)
    target_link_libraries(portable_load_job_tests PRIVATE mksc_runtime)
    add_test(NAME portable_load_job COMMAND portable_load_job_tests)
    set_tests_properties(portable_load_job PROPERTIES ENVIRONMENT
        "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_MAC_BACKGROUND_APP=1;SDL_WINDOW_ACTIVATE_WHEN_SHOWN=0;SDL_WINDOW_ACTIVATE_WHEN_RAISED=0")
    portable_check_group(startup_loading portable_load_job)
endif()

# Optional owner cartridge check; synthetic MK64 conversion always runs above.
set(PORTABLE_MK64_ROM "" CACHE FILEPATH "Owner Mario Kart 64 (US) ROM, read only")
if(PORTABLE_MK64_ROM)
    if(NOT EXISTS "${PORTABLE_MK64_ROM}")
        message(FATAL_ERROR "ROM does not exist: ${PORTABLE_MK64_ROM}")
    endif()
    add_test(NAME portable_mk64_import_rom COMMAND portable_mk64_import_tests --mk64 "${PORTABLE_MK64_ROM}")
    set_tests_properties(portable_mk64_import_rom PROPERTIES TIMEOUT 120)
    add_test(NAME portable_mk64_import_cli COMMAND mksc --import-game "${PORTABLE_MK64_ROM}"
        "${CMAKE_CURRENT_BINARY_DIR}/mk64-import/mods")
    set_tests_properties(portable_mk64_import_cli PROPERTIES TIMEOUT 120
        PASS_REGULAR_EXPRESSION "Imported Mario Kart 64: 16 courses in 4 cups race; 4 battle stages as exploration previews")
endif()

if(PORTABLE_BUILD_SDL3_APP AND PORTABLE_ROM)
    set(mk64_fixture_work "${CMAKE_CURRENT_BINARY_DIR}/mk64-fixture-routes")
    add_test(NAME portable_mk64_routes_reset COMMAND "${CMAKE_COMMAND}" -E rm -rf "${mk64_fixture_work}")
    set_tests_properties(portable_mk64_routes_reset PROPERTIES FIXTURES_SETUP mk64_fixture_clean)
    add_test(NAME portable_mk64_routes COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/portable/tests/test_mk64_routes.py"
        --binary $<TARGET_FILE:mksc> --pack "${menu_tour}/pack"
        --fixture $<TARGET_FILE:portable_mk64_import_tests> --work "${mk64_fixture_work}"
        --gpu-probe $<TARGET_FILE:portable_render_gpu_tests> --video-driver "${render_gpu_video}")
    set_tests_properties(portable_mk64_routes PROPERTIES TIMEOUT 180 PORTABLE_GPU_ROUTE TRUE SKIP_RETURN_CODE 77
        FIXTURES_REQUIRED "menu_tour_pack;mk64_fixture_clean"
        PASS_REGULAR_EXPRESSION "MK64 routes: PASS")
    if(PORTABLE_MK64_ROM)
        # Owner cartridge: an aided three-lap Time Trial and a Mushroom Cup GP.
        set(mk64_race_work "${CMAKE_CURRENT_BINARY_DIR}/mk64-race")
        add_test(NAME portable_mk64_race_reset COMMAND "${CMAKE_COMMAND}" -E rm -rf "${mk64_race_work}")
        set_tests_properties(portable_mk64_race_reset PROPERTIES FIXTURES_SETUP mk64_race_clean)
        add_test(NAME portable_mk64_race COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/portable/tests/test_mk64_routes.py"
            --binary $<TARGET_FILE:mksc> --pack "${menu_tour}/pack"
            --fixture $<TARGET_FILE:portable_mk64_import_tests> --work "${mk64_race_work}"
            --gpu-probe $<TARGET_FILE:portable_render_gpu_tests> --video-driver "${render_gpu_video}"
            --mk64-rom "${PORTABLE_MK64_ROM}")
        set_tests_properties(portable_mk64_race PROPERTIES TIMEOUT 180 PORTABLE_GPU_ROUTE TRUE SKIP_RETURN_CODE 77
            FIXTURES_REQUIRED "menu_tour_pack;mk64_race_clean"
            PASS_REGULAR_EXPRESSION "MK64 race: PASS")
    endif()
endif()
