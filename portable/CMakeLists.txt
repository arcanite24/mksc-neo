cmake_minimum_required(VERSION 3.20)

project(mksc_portable LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # 32-bit readdir/stat must accept modern filesystem inode/offset widths.
    add_compile_definitions(_FILE_OFFSET_BITS=64)
endif()

option(PORTABLE_STRICT_WARNINGS "Treat native compiler warnings as errors" OFF)
option(PORTABLE_SANITIZERS "Enable address and undefined-behavior sanitizers" OFF)
if(PORTABLE_STRICT_WARNINGS)
    if(MSVC)
        # Match the GCC/Clang -Wall -Wextra set: implicit numeric conversions
        # (C4244, C4305), shadowing (C4456, C4457, C4459) and constant
        # conditions (C4127) are not errors there either.
        add_compile_options(/W4 /WX /wd4244 /wd4305 /wd4456 /wd4457 /wd4459 /wd4127)
    else()
        add_compile_options(-Wall -Wextra -Wpedantic -Werror)
        if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
            # Bounded snprintf truncation is intentional (labels, notices, paths).
            add_compile_options(-Wno-format-truncation)
        endif()
    endif()
endif()
if(MSVC)
    # Standard C file APIs are intentional; their signatures are portable.
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()
if(PORTABLE_SANITIZERS)
    if(MSVC)
        message(FATAL_ERROR "ASan+UBSan checks require a GNU/Clang toolchain")
    endif()
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined -fno-sanitize-recover=all)
endif()

option(PORTABLE_BUILD_SDL3_APP "Build the SDL3 interactive app (requires SDL3)" ON)

option(PORTABLE_BUILD_TESTS "Build the portable host tests" ON)

add_library(mksc_world_actors STATIC core/ambient.c core/course_prop.c)
target_include_directories(mksc_world_actors PUBLIC core)

add_subdirectory(third_party/lua)
add_subdirectory(third_party/stb)

# Fonts and the menu names table, embedded so the modern menus need no files.
set(portable_embedded_files
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/NunitoBlack.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/NunitoExtraBold.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/NunitoBlackItalic.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/data/menu-names.json")
string(REPLACE ";" "|" portable_embedded_list "${portable_embedded_files}")
set(portable_embedded_source "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_files.c")
add_custom_command(OUTPUT "${portable_embedded_source}"
    COMMAND "${CMAKE_COMMAND}" "-DOUTPUT=${portable_embedded_source}" "-DFILES=${portable_embedded_list}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/embed_files.cmake"
    DEPENDS ${portable_embedded_files} embed_files.cmake VERBATIM)

# Native ROM importer: pure bytes-to-pack, byte-identical to import_rom.py.
add_library(mksc_rom_import STATIC
    import/rom_import.c import/rom_import_core.c import/rom_import_image.c
    import/rom_import_drivers.c import/rom_import_overlays.c import/rom_import_courses.c
    import/rom_import_course_race.c import/rom_import_ambient.c
    import/rom_import_audio.c import/rom_import_audio_bank.c import/rom_import_audio_sequence.c
    import/rom_import_frontend.c import/rom_import_frontend_title.c
    import/rom_import_frontend_menus.c import/rom_import_frontend_select.c import/rom_zip.c
    import/nds/nds_fs.c import/nds/mkds_import.c import/nds/nitro_g3d.c import/nds/mkds_course.c import/nds/mkds_map.c
    import/nds/mkds_model.c import/nds/nitro_2d.c
    import/nds/mkds_driver.c import/nds/mkds_fx.c import/nds/nitro_spa.c import/nds/mkds_sound.c import/nds/mkds_rules.c
    import/nds/mkds_item_art.c
    import/n64/mk64_decode.c import/n64/mk64_fx.c import/n64/mk64_course.c import/n64/mk64_import.c
    import/n64/mk64_object.c import/n64/mk64_driver.c import/n64/mk64_sound.c)
target_include_directories(mksc_rom_import PUBLIC import import/nds import/n64)
# Deterministic Q16/Q30 vector math, shared by the importer and the 3D runtime.
add_library(mksc_fixed_3d STATIC core/fixed_3d.c)
target_include_directories(mksc_fixed_3d PUBLIC core)
target_link_libraries(mksc_rom_import PRIVATE mksc_fixed_3d)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(modern_drift_header "${CMAKE_CURRENT_BINARY_DIR}/generated/modern_drift_payload.h")
add_custom_command(OUTPUT "${modern_drift_header}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../tools/portable/modern_drift_format.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/data/modern-drift.json" --header "${modern_drift_header}"
    DEPENDS data/modern-drift.json ../tools/portable/modern_drift_format.py VERBATIM)

set(modern_air_header "${CMAKE_CURRENT_BINARY_DIR}/generated/modern_air_payload.h")
add_custom_command(OUTPUT "${modern_air_header}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../tools/portable/modern_drift_format.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/data/modern-air.json" --header "${modern_air_header}"
    DEPENDS data/modern-air.json ../tools/portable/modern_drift_format.py VERBATIM)

set(title_logo_header "${CMAKE_CURRENT_BINARY_DIR}/generated/title_logo.h")
add_custom_command(OUTPUT "${title_logo_header}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../tools/portable/title_logo.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/../docs/branding/mksc-neo.png" --header "${title_logo_header}"
    DEPENDS ../docs/branding/mksc-neo.png ../tools/portable/title_logo.py VERBATIM)

# Store payload contains only repository-authored Lua examples, never ROM data.
set(store_payload "${CMAKE_CURRENT_BINARY_DIR}/generated/store_payload.h")
file(GLOB store_example_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../mods/examples/*/main.lua" "${CMAKE_CURRENT_SOURCE_DIR}/../mods/examples/*/mod.json")
add_custom_command(OUTPUT "${store_payload}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../tools/portable/build_store.py" --output "${store_payload}"
    DEPENDS ../tools/portable/build_store.py ../stores/official/index.json ${store_example_sources} VERBATIM)
find_package(CURL 7.85 QUIET)

set(editor_physics_header "${CMAKE_CURRENT_BINARY_DIR}/generated/editor_physics.h")
add_custom_command(OUTPUT "${editor_physics_header}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../tools/portable/mesh_kart_format.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/data/mesh-kart.json" --output "${editor_physics_header}" --c-header editor_physics
    DEPENDS data/mesh-kart.json ../tools/portable/mesh_kart_format.py VERBATIM)
set(editor_items_header "${CMAKE_CURRENT_BINARY_DIR}/generated/editor_items.h")
add_custom_command(OUTPUT "${editor_items_header}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../tools/portable/mesh_item_format.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/data/mesh-items.json" --output "${editor_items_header}" --c-header editor_items
    DEPENDS data/mesh-items.json ../tools/portable/mesh_item_format.py VERBATIM)

add_library(mksc_runtime STATIC
    "${store_payload}" platform/mod_store.c app/course_editor.c platform/course_editor_pack.c "${editor_physics_header}" "${editor_items_header}"
    core/mesh_query.c core/mesh_item_motion.c core/mesh_kart.c core/mesh_kart_rules.c core/mesh_motion.c app/mesh_collision_index.c app/mesh_course_actors.c app/mesh_course_coins.c app/mesh_course_coin_turn.c app/mesh_course_capture.c app/mesh_course_crush.c app/mesh_course_strong_hit.c app/mesh_racer.c app/mesh_race.c app/mesh_battle.c app/mesh_battle_pilot.c app/mesh_cpu.c app/mesh_time_trial.c app/mesh_time_trial_replay.c app/mesh_time_trial_pilot.c
    app/mesh_sky.c app/mesh_sky_progress.c app/mesh_palette.c app/mesh_cup.c app/mesh_item_inventory.c app/mesh_item_boxes.c app/mesh_item_status.c app/mesh_item_target.c app/mesh_items.c app/mesh_items_state.c platform/mesh_cup_store.c
    platform/mesh_time_trial_store.c platform/mesh_time_trial_cli.c
    "${modern_drift_header}" "${modern_air_header}" platform/driving_profile.c assets/modern_drift_asset.c core/modern_drift.c core/modern_air.c
    app/frontend_config.c
    platform/startup_trace.c platform/content_import.c platform/rom_import_host.c platform/game_import_host.c platform/ui_background_host.c platform/first_run.c
    platform/file_read.c platform/source_path.c
    platform/json_reader.c platform/mod_manifest.c platform/mod_loader.c platform/mod_manager.c platform/game_library.c platform/mod_install.c platform/file_dialog.c platform/mod_menu_renderer.c platform/mod_archive.c platform/mod_identity.c core/text_number.c platform/game_data.c platform/ui_text_pack.c platform/text_data.c platform/kart_data.c
    platform/settings.c platform/script_host.c
    core/driver_collision.c
    core/driver_track_response.c
    core/driver_terrain_effect.c
    core/driver_track_state.c
    core/handling_trace.c
    assets/track_asset.c
    assets/source_course_metadata.c
    assets/track_object_asset.c
    assets/track_object_art.c
    assets/track_object_decoder.c
    assets/track_runtime_map.c
    assets/terrain_asset.c
    assets/sky_asset.c assets/ambient_asset.c assets/character_view_asset.c assets/finish_asset.c assets/weather_asset.c assets/recovery_fx_asset.c assets/surface_trail_asset.c assets/coin_effect_asset.c assets/impact_effect_asset.c
    assets/track_tileset_asset.c
    assets/road_palette_asset.c
    assets/source_motion_tables.c
    assets/source_normal_response_table.c
    assets/source_vector_tables.c
    assets/source_b550_tables.c
    assets/terrain_effect_tables.c
    assets/road_map_asset.c
    assets/minimap_asset.c
    assets/hud_asset.c
    app/gp_coin_hud.c
    app/time_trial_flow.c platform/recovered_race_smoke.c platform/diagnostic.c app/race_runtime.c
    app/gp_race_session.c app/course_contacts.c core/battle_navigation.c app/gp_race_presentation.c
    app/gp_item_art_clock.c
    core/gp_entry.c
    assets/gp_opening_asset.c platform/gp_opening_renderer.c
    app/gp_opening_camera.c
    app/gp_opening_veil.c
    assets/item_box_asset.c
    assets/coin_loss_asset.c
    app/gp_start_lakitu.c
    app/gp_rank_hud.c
    app/gp_turn_cue.c
    app/gp_driving_quality.c
    app/gp_race_results.c app/gp_cup_session.c app/gp_cup_scoring.c
    app/gp_failure_menu.c app/gp_result_clock.c
    app/gp_ceremony_session.c
    platform/gp_progress_store.c
    assets/gp_ceremony_asset.c
    app/gp_standings_session.c
    app/gp_results_flow.c app/gp_results_session.c
    core/boo_attack.c app/gp_boo.c app/gp_race_roster.c app/gp_roster_items.c app/gp_item_pool.c app/gp_item_inventory.c app/gp_green_shell.c app/gp_shell_orbit.c
    app/gp_red_shell.c
    app/gp_banana.c
    app/time_trial_ghost_playback.c
    app/frontend_flow.c app/mesh_cup_menu.c app/frontend_input_replay.c app/replay_stream.c platform/frontend_replay_file.c platform/recovered_replay_file.c app/local_players.c
    app/frontend_presentation.c
    app/pause_menu.c
    app/recovered_time_trial_session.c app/recovered_time_trial_replay.c
    platform/recovered_time_trial_host.c app/time_trial_result.c
    platform/time_trial_ghost_store.c app/time_trial_ghost_save.c
    platform/time_trial_ghost_library.c platform/time_trial_ghost_exchange.c
    assets/asset_pack.c assets/native_image.c assets/course_3d.c assets/mesh_kart_rules_asset.c assets/mesh_item_rules_asset.c
    platform/gp_countdown_renderer.c platform/title_renderer.c platform/frontend_image_renderer.c platform/menu_chrome_renderer.c platform/menu_font.c platform/character_menu_renderer.c platform/course_menu_renderer.c platform/pause_menu_renderer.c
    platform/ui_canvas.c platform/ui_draw_list.c platform/ui_font.c platform/ui_art.c platform/menu_names.c
    platform/modern_menu.c platform/modern_menu_pages.c platform/modern_menu_select.c platform/menu_driver_model.c
    platform/modern_menu_options.c platform/modern_menu_mods.c platform/modern_menu_games.c platform/modern_menu_setup.c platform/modern_menu_pause.c platform/graphics_options.c
    platform/modern_menu_results.c platform/modern_menu_transition.c platform/modern_menu_loading.c "${portable_embedded_source}" "${title_logo_header}"
    assets/character_asset.c assets/driver_projection_asset.c assets/driver_sprite_asset.c assets/sound_set_asset.c assets/fx_set_asset.c platform/mesh_driver_art.c platform/mesh_battle_art.c
    assets/recovered_pack.c assets/content_registry.c
    assets/presentation_pack.c assets/race_particle_asset.c assets/gp_race_hud_asset.c assets/course_mirror.c
    assets/road_window.c
    platform/personal_best.c platform/time_trial_records.c
    platform/software_frame.c platform/ui_text.c platform/render_scene.c platform/render_pose.c platform/render_object_animation.c platform/render_chase.c platform/render_vfx.c platform/render_kart_animation.c platform/mesh_kart_fx_art.c platform/mesh_fx_set.c platform/render_model.c platform/mesh_sky_scene.c platform/mesh_palette_scene.c platform/scene_3d.c platform/scene_3d_workers.c platform/race_ground_3d.c
    assets/model_asset.c assets/asset_sources.c platform/model_renderer.c
    platform/sky_renderer.c
    platform/character_renderer.c platform/road_renderer.c
    platform/hud_renderer.c platform/mesh_item_art.c platform/mesh_item_scene.c platform/mesh_item_audio.c app/gp_item_hud.c platform/race_scene_renderer.c
    platform/green_shell_renderer.c
    platform/item_box_renderer.c
    platform/lightning_effect_renderer.c
    platform/gp_finish_banner_renderer.c
    platform/gp_results_renderer.c
    platform/gp_standings_renderer.c
    platform/gp_ceremony_renderer.c platform/gp_progress_renderer.c
    platform/red_shell_renderer.c
    platform/banana_renderer.c platform/boo_attack_renderer.c
    platform/race_particle_renderer.c
    platform/frame_scheduler.c platform/controller_input.c platform/race_interpolation.c
    core/camera_state.c core/camera_projection.c core/camera_affine.c
    core/character_camera.c
    core/track_geometry.c
    core/race_session.c core/route_geometry.c core/driver_pilot.c core/driver_angle_composition.c core/driver_angle_target.c core/driver_material.c
    core/driver_hop.c
    core/driver_mushroom.c
    core/time_trial_ghost.c
    core/water_recovery.c core/driver_water_recovery.c
    core/driver_pose.c core/driver_input.c core/driver_acceleration.c
    core/driver_brake.c core/driver_forward_acceleration.c
    core/driver_angle_callbacks.c
    core/driver_forward_limit.c core/driver_steering.c
    core/kart_motion.c core/driver_vector_effect.c
    core/driver_b550_resultant.c core/collision.c
    core/terrain_query.c core/ceremony_motion.c assets/track_raster.c
    assets/track_material.c assets/normal_response.c assets/engine_sound_asset.c)
target_compile_features(mksc_runtime PUBLIC c_std_11)
target_include_directories(mksc_runtime PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_include_directories(mksc_runtime PUBLIC app core assets
                           platform)
target_link_libraries(mksc_runtime PUBLIC mksc_world_actors portable_lua mksc_rom_import mksc_fixed_3d portable_stb)
if(UNIX)
    target_link_libraries(mksc_runtime PUBLIC m)
endif()

if(PORTABLE_BUILD_SDL3_APP)
    if(NOT TARGET SDL3::SDL3) # Android builds SDL from source beside the game.
        find_package(SDL3 REQUIRED CONFIG COMPONENTS SDL3)
    endif()
    target_compile_definitions(mksc_runtime PUBLIC PORTABLE_ENABLE_SDL3=1)
    target_sources(mksc_runtime PRIVATE
        platform/course_editor_host.c platform/input_script.c platform/game_host.c platform/mod_store_job.c platform/course_3d_host.c platform/modern_menu_host.c platform/game_audio.c platform/sdl_gamepad.c platform/input_display.c
        platform/mobile_touch.c platform/render_gpu.c platform/render_gles.c platform/ui_gpu.c platform/ui_geometry.c platform/ui_gles_font.c
        platform/mobile_lifecycle.c platform/load_job.c platform/loading_screen.c platform/startup_pack.c
        platform/gp_race_host.c platform/multi_race_host.c platform/gp_results_host.c platform/gp_ceremony_host.c
        app/frontend_audio.c app/race_audio_timeline.c app/race_rumble.c
        audio/engine_control.c
        audio/audio_command_stream.c
        audio/audio_session.c audio/audio_pack_song.c
        audio/audio_song_runtime.c audio/audio_sequence_asset.c
        audio/audio_sequence_player.c audio/audio_voice_bank_asset.c
        audio/audio_pcm_mixer.c audio/audio_psg_mixer.c
        audio/audio_sample_asset.c audio/nds_sequence.c audio/n64_sequence.c platform/sdl_audio_queue.c)
    target_include_directories(mksc_runtime PUBLIC audio)
    target_link_libraries(mksc_runtime PUBLIC SDL3::SDL3)
endif()

if(ANDROID)
    # SDLActivity loads libmksc.so and calls its SDL_main.
    add_library(mksc SHARED platform/main.c)
else()
    add_executable(mksc platform/main.c)
endif()
target_link_libraries(mksc PRIVATE mksc_runtime)
if(EMSCRIPTEN)
    if(NOT PORTABLE_BUILD_SDL3_APP)
        message(FATAL_ERROR "The browser build requires the SDL3 application")
    endif()
    set_target_properties(mksc PROPERTIES SUFFIX ".html"
        LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/platform/browser.html")
    target_link_options(mksc PRIVATE
        "--shell-file" "${CMAKE_CURRENT_SOURCE_DIR}/platform/browser.html"
        "-sALLOW_MEMORY_GROWTH=1" "-sSTACK_SIZE=1048576"
        "-sEXPORTED_RUNTIME_METHODS=FS,callMain" "-sFORCE_FILESYSTEM=1"
        "-sINVOKE_RUN=0" "-sEXIT_RUNTIME=0" "-lidbfs.js")
    # Offline cache for the page and engine (see browser.html).
    configure_file(platform/browser_sw.js "${CMAKE_CURRENT_BINARY_DIR}/sw.js" COPYONLY)
endif()
option(PORTABLE_WINDOWS_GUI "Build mksc.exe without a console window (player bundles)" OFF)
if(WIN32 AND PORTABLE_WINDOWS_GUI)
    set_target_properties(mksc PROPERTIES WIN32_EXECUTABLE ON)
    target_compile_definitions(mksc PRIVATE PORTABLE_WINDOWS_GUI=1)
endif()

if(PORTABLE_BUILD_TESTS)
    add_executable(test_course_editor tests/test_course_editor.c)
    target_link_libraries(test_course_editor PRIVATE mksc_runtime)
    add_test(NAME portable_course_editor COMMAND test_course_editor)
    if(PORTABLE_BUILD_SDL3_APP)
        add_executable(test_course_editor_host tests/test_course_editor_host.c)
        target_link_libraries(test_course_editor_host PRIVATE mksc_runtime)
        add_test(NAME portable_course_editor_host COMMAND test_course_editor_host)
        set_tests_properties(portable_course_editor_host PROPERTIES ENVIRONMENT
            "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_MAC_BACKGROUND_APP=1;SDL_WINDOW_ACTIVATE_WHEN_SHOWN=0;SDL_WINDOW_ACTIVATE_WHEN_RAISED=0")
    endif()
    target_compile_definitions(mksc_runtime PRIVATE PORTABLE_ENABLE_TEST_HOOKS=1)
    enable_testing()
    add_subdirectory(tests)
endif()

# Pack SHA-256 walks tens of MiB repeatedly. Keep this pure, tested reader
# optimized in developer builds too; the remaining gameplay stays debuggable.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set_property(SOURCE assets/asset_pack.c APPEND PROPERTY COMPILE_OPTIONS "$<$<CONFIG:Debug>:-O2>")
    if(PORTABLE_BUILD_TESTS)
        set_property(SOURCE assets/asset_pack.c DIRECTORY tests APPEND PROPERTY COMPILE_OPTIONS "$<$<CONFIG:Debug>:-O2>")
    endif()
endif()

if(CURL_FOUND)
    target_compile_definitions(mksc_runtime PRIVATE PORTABLE_STORE_CURL=1)
    target_link_libraries(mksc_runtime PRIVATE CURL::libcurl)
else()
    message(STATUS "libcurl >= 7.85 absent: Store supports built-in/local sources only")
endif()
