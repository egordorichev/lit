cmake_minimum_required(VERSION 3.10)
project(embedfile)

add_executable(embedfile embed_file.c)

if (LIT_BUILD_ANDROID)
	set_target_properties(embedfile PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../dist/${CMAKE_ANDROID_ARCH_ABI}")
else()
	set_target_properties(embedfile PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../dist")
endif()
