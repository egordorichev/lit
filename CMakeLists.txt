cmake_minimum_required(VERSION 3.10)
project(lit C)

set(CMAKE_C_STANDARD 11)
include_directories(include)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/dist")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/dist")

OPTION(DEFINE_TEST "Build code for testing (disables debug output)" OFF)
OPTION(DEFINE_OPTIMIZE "Build code for deployment (disables debug and enables optimisation)" OFF)

if (EMSCRIPTEN)
 set(CMAKE_AR "emcc")
 set(CMAKE_STATIC_LIBRARY_SUFFIX ".bc")
 set(CMAKE_C_CREATE_STATIC_LIBRARY "<CMAKE_AR> -o <TARGET> <LINK_FLAGS> <OBJECTS>")
 set(CMAKE_CXX_CREATE_STATIC_LIBRARY "<CMAKE_AR> -o <TARGET> <LINK_FLAGS> <OBJECTS>")
endif()

IF(DEFINE_TEST)
 MESSAGE("Adding Test flag...")
 ADD_DEFINITIONS(-DTEST)
ENDIF(DEFINE_TEST)

IF(DEFINE_OPTIMIZE)
 MESSAGE("Adding Optimize flag...")
 ADD_DEFINITIONS(-DTEST)
 set(CMAKE_C_FLAGS "-O3")
ENDIF(DEFINE_OPTIMIZE)

add_library(lit STATIC src/lit/mem/lit_mem.c src/lit/state/lit_state.c src/lit/vm/lit_chunk.c
        src/lit/debug/lit_debug.c src/lit/vm/lit_value.c src/lit/vm/lit_vm.c src/lit/scanner/lit_scanner.c
        src/lit/parser/lit_parser.c src/lit/parser/lit_ast.c src/lit/emitter/lit_emitter.c src/lit/vm/lit_object.c
        src/lit/util/lit_table.c src/lit/util/lit_array.c src/lit/util/lit_fs.c src/lit/api/lit_api.c
        src/lit/std/lit_core.c src/lit/std/lit_math.c src/lit/std/lit_file.c src/lit/parser/lit_error.c
        )

add_executable(litc src/main.c)
target_link_libraries(litc lit)
target_link_libraries(litc m) # Lib math