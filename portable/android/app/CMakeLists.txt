cmake_minimum_required(VERSION 3.20)
project(mksc_android C)

# SDL builds from source beside the game; the portable tree then links
# SDL3::SDL3 and produces libmksc.so for SDLActivity.
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${MKSC_SDL_SOURCE}" SDL)

set(PORTABLE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../.." portable)
