cmake_minimum_required(VERSION 3.10)
project(lit C)

set(CMAKE_C_STANDARD 11)
include_directories(include)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/dist")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/dist")

if(NOT CMAKE_BUILD_TYPE)
 set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_C_FLAGS "-Wall -Wextra -Wno-unused-variable -Wno-unused-parameter -Wno-unused-function -Wno-strict-aliasing -Wno-unused-result -Wno-sequence-point")
set(CMAKE_C_FLAGS_DEBUG "-g")
set(CMAKE_C_FLAGS_RELEASE "-O3")

OPTION(DEFINE_TEST "Build code for testing (disables debug output)" OFF)

if (EMSCRIPTEN)
 set(CMAKE_AR "emcc")
 set(CMAKE_STATIC_LIBRARY_SUFFIX ".bc")
 set(CMAKE_C_CREATE_STATIC_LIBRARY "<CMAKE_AR> -o <TARGET> <LINK_FLAGS> <OBJECTS>")
 set(CMAKE_CXX_CREATE_STATIC_LIBRARY "<CMAKE_AR> -o <TARGET> <LINK_FLAGS> <OBJECTS>")
 ADD_DEFINITIONS(-DEMSCRIPTEN)
endif()

IF(DEFINE_TEST)
 MESSAGE("Adding Testing flag...")
 ADD_DEFINITIONS(-DTESTING)
ELSE()
 MESSAGE("Adding Release flag...")
 ADD_DEFINITIONS(-DRELEASE)
ENDIF(DEFINE_TEST)

add_library(lit STATIC src/lit/mem/lit_mem.c src/lit/state/lit_state.c src/lit/vm/lit_chunk.c
 src/lit/debug/lit_debug.c src/lit/vm/lit_value.c src/lit/vm/lit_vm.c src/lit/scanner/lit_scanner.c
 src/lit/parser/lit_parser.c src/lit/parser/lit_ast.c src/lit/emitter/lit_emitter.c src/lit/vm/lit_object.c
 src/lit/util/lit_table.c src/lit/util/lit_array.c src/lit/util/lit_fs.c src/lit/api/lit_api.c
 src/lit/std/lit_core.c src/lit/std/lit_math.c src/lit/std/lit_file.c src/lit/parser/lit_error.c
 src/lit/optimizer/lit_optimizer.c src/lit/util/lit_utf.c)

add_executable(litc src/main.c)
target_link_libraries(litc lit)
target_link_libraries(litc m) # Lib math

IF(NOT EMSCRIPTEN)
 target_link_libraries(litc readline)
ENDIF()

set_target_properties(litc PROPERTIES OUTPUT_NAME "lit")
install(TARGETS litc DESTINATION bin)