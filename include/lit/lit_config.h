#ifndef LIT_CONFIG_H
#define LIT_CONFIG_H

#define LIT_REPOSITORY "https://github.com/egordorichev/lit"

#define LIT_VERSION_MAJOR 0
#define LIT_VERSION_MINOR 2
#define LIT_VERSION_STRING "0.2"
#define LIT_BYTECODE_VERSION 0

// #define TESTING
#define DEBUG

#ifdef DEBUG
#define LIT_TRACE_EXECUTION
#define LIT_TRACE_CHUNK
// #define LIT_MINIMIZE_CONTAINERS
#define LIT_LOG_GC
// #define LIT_LOG_ALLOCATION
// #define LIT_LOG_MARKING
// #define LIT_LOG_BLACKING
// #define LIT_STRESS_TEST_GC
#endif

#ifdef TESTING
// So that we can actually test the map contents with a single-line expression
#define SINGLE_LINE_MAPS
#define SINGLE_LINE_MAPS_ENABLED true

// Make sure that we did not break anything
// #define LIT_STRESS_TEST_GC
#else
#define SINGLE_LINE_MAPS_ENABLED false
#endif

#define LIT_INTERPOLATION_NESTING_MAX 4
#define LIT_REGISTERS_MAX 250 // Can't be over 255

#define LIT_GC_HEAP_GROW_FACTOR 2
#define LIT_CALL_FRAMES_MAX 64
#define LIT_INITIAL_CALL_FRAMES 4
#define LIT_CONTAINER_OUTPUT_MAX 10

#if defined(__ANDROID__) || defined(_ANDROID_)
#define LIT_OS_ANDROID
#elif defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
#define LIT_OS_WINDOWS
#elif __APPLE__
#define LIT_OS_MAC
#define LIT_OS_UNIX_LIKE
#elif __linux__
#define LIT_OS_LINUX
#define LIT_OS_UNIX_LIKE
#else
#define LIT_OS_UNKNOWN
#endif

#endif