# Lua 5.4.7, unmodified sources from https://www.lua.org/ftp/lua-5.4.7.tar.gz
# (verified against the published SHA-256). Only the core, auxiliary library
# and the base/string/table/math libraries are kept; io, os, package, debug,
# coroutine and utf8 are deliberately absent. See LICENSE.

# Vendored code keeps upstream warnings out of the strict project flags.
get_directory_property(portable_lua_options COMPILE_OPTIONS)
list(REMOVE_ITEM portable_lua_options -Wall -Wextra -Wpedantic -Werror /W4 /WX)
set_directory_properties(PROPERTIES COMPILE_OPTIONS "${portable_lua_options}")

add_library(portable_lua STATIC
    src/lapi.c src/lauxlib.c src/lbaselib.c src/lcode.c src/lctype.c
    src/ldebug.c src/ldo.c src/ldump.c src/lfunc.c src/lgc.c src/llex.c
    src/lmathlib.c src/lmem.c src/lobject.c src/lopcodes.c src/lparser.c
    src/lstate.c src/lstring.c src/lstrlib.c src/ltable.c src/ltablib.c
    src/ltm.c src/lundump.c src/lvm.c src/lzio.c)
set_target_properties(portable_lua PROPERTIES LINKER_LANGUAGE C)
target_include_directories(portable_lua PUBLIC src PRIVATE .)
# Fixed string-hash seed and sort pivots: the same script sees the same table
# layout on every run, so hook behavior cannot depend on time or ASLR.
target_compile_definitions(portable_lua PRIVATE "LUA_USER_H=\"mksc_lua_user.h\"")
if(UNIX)
    target_link_libraries(portable_lua PUBLIC m)
endif()
