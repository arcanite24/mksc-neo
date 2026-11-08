# stb_truetype v1.26 (public domain), unmodified, from
# https://github.com/nothings/stb. Used only by the modern menu font renderer.

# Vendored code keeps upstream warnings out of the strict project flags.
get_directory_property(portable_stb_options COMPILE_OPTIONS)
list(REMOVE_ITEM portable_stb_options -Wall -Wextra -Wpedantic -Werror /W4 /WX)
set_directory_properties(PROPERTIES COMPILE_OPTIONS "${portable_stb_options}")

add_library(portable_stb STATIC stb_truetype.c)
target_include_directories(portable_stb PUBLIC .)
if(UNIX)
    target_link_libraries(portable_stb PUBLIC m)
endif()
