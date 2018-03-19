project(searoute)

add_subdirectory(libpng)

include_directories(libpng)
add_executable(searoute searoute.c searoute.h)
target_link_libraries(searoute png_static)
