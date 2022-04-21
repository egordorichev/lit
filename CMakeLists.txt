cmake_minimum_required(VERSION 3.10)
project(lit C)

set(CMAKE_C_STANDARD 11)
include_directories(include/lit)

if (NOT CMAKE_BUILD_TYPE)
 set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_C_FLAGS "-no-pie -Wall -Wextra -Wno-unused-variable -Wno-unused-parameter -Wno-unused-function -Wno-strict-aliasing -Wno-unused-result -Wno-sequence-point ${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS_DEBUG "-g ${CMAKE_C_FLAGS_DEBUG}")
set(CMAKE_C_FLAGS_RELEASE "-O3 ${CMAKE_C_FLAGS_RELEASE}")

option(LIT_DEFINE_TEST "Build code for testing (disables debug output)" OFF)
option(LIT_STANDALONE "Build in standalone mode" OFF)
option(LIT_BUILD_BINARY "Build the binary" ON)

if (EMSCRIPTEN)
 set(CMAKE_AR "emcc")
 set(CMAKE_STATIC_LIBRARY_SUFFIX ".bc")
 set(CMAKE_C_CREATE_STATIC_LIBRARY "<CMAKE_AR> -o <TARGET> <LINK_FLAGS> <OBJECTS>")
 set(CMAKE_CXX_CREATE_STATIC_LIBRARY "<CMAKE_AR> -o <TARGET> <LINK_FLAGS> <OBJECTS>")
 ADD_DEFINITIONS(-DEMSCRIPTEN)
endif()

if (DEFINE_TEST)
 message("Adding Testing flag...")
 add_definitions(-DTESTING)
else()
 message("Adding release flag...")
 add_definitions(-DRELEASE)
endif(DEFINE_TEST)

if (COVERAGE)
 message("Adding coverage flag...")
 set(CMAKE_C_FLAGS "--coverage ${CMAKE_C_FLAGS}")
endif(COVERAGE)

add_library(lit STATIC
 src/lit/mem/lit_mem.c src/lit/state/lit_state.c src/lit/vm/lit_chunk.c
 src/lit/debug/lit_debug.c src/lit/vm/lit_value.c src/lit/vm/lit_vm.c src/lit/scanner/lit_scanner.c
 src/lit/parser/lit_parser.c src/lit/parser/lit_ast.c src/lit/emitter/lit_emitter.c src/lit/vm/lit_object.c
 src/lit/util/lit_table.c src/lit/util/lit_array.c src/lit/util/lit_fs.c src/lit/api/lit_api.c src/lit/api/lit_calls.c
 src/lit/std/lit_core.c src/lit/std/lit_math.c src/lit/std/lit_file.c src/lit/std/lit_gc.c src/lit/parser/lit_error.c
 src/lit/optimizer/lit_optimizer.c src/lit/util/lit_utf.c src/lit/preprocessor/lit_preprocessor.c

 src/lit/un/parser/un_parser.c src/lit/un/scanner/un_scanner.c)

target_link_libraries(lit LINK_PUBLIC m) # Lib math

if (LIT_BUILD_ANDROID)
 set_target_properties(lit PROPERTIES ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist/${CMAKE_ANDROID_ARCH_ABI}")
else()
 set_target_properties(lit PROPERTIES ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist")
endif()

install(TARGETS lit DESTINATION lib)
install(DIRECTORY include/lit/ DESTINATION include/lit)

if (LIT_BUILD_BINARY)
 if (STANDALONE)
  message("Adding standalone flag...")
  add_executable(litc src/lit/standalone/main.c bytecode.c)
 else()
  message("Building regular binary...")
  add_executable(litc src/lit/cli/main.c)
 endif()

 target_link_libraries(litc lit)

 if(NOT EMSCRIPTEN AND NOT STANDALONE AND UNIX)
  target_link_libraries(litc readline)
 endif()

 if (LIT_BUILD_ANDROID)
  set_target_properties(litc PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist/${CMAKE_ANDROID_ARCH_ABI}")
  set_target_properties(litc PROPERTIES ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist/${CMAKE_ANDROID_ARCH_ABI}")
 else()
  set_target_properties(litc PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist")
  set_target_properties(litc PROPERTIES ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist")
 endif()

 set_target_properties(litc PROPERTIES OUTPUT_NAME "lit")
 install(TARGETS litc DESTINATION bin)
endif()
