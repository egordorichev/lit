cmake_minimum_required(VERSION 3.10)
project(lit C)

set(CMAKE_C_STANDARD 11)
include_directories(include)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/dist")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/dist")

add_library(lit STATIC src/lit/lit.c src/lit/mem/lit_mem.c src/lit/state/lit_state.c src/lit/vm/lit_chunk.c src/lit/debug/lit_debug.c src/lit/vm/lit_value.c)
add_executable(litc src/main.c)

target_link_libraries(litc lit)