#ifndef LIT_CONFIG_H
#define LIT_CONFIG_H

#define LIT_VERSION_MAJOR 0
#define LIT_VERSION_MINOR 1
#define LIT_VERSION_STRING "0.1"

#define TEST

#ifndef TEST
#define LIT_TRACE_EXECUTION
#define LIT_TRACE_STACK
// #define LIT_TRACE_CHUNK
// #define LIT_STRESS_TEST_GC
// #define LIT_LOG_GC
#endif

#define LIT_MAX_INTERPOLATION_NESTING 4

#define LIT_GC_HEAP_GROW_FACTOR 2
#define LIT_CALL_FRAMES_MAX 32
#define LIT_STACK_MAX (LIT_CALL_FRAMES_MAX * 8)
#define LIT_ROOT_MAX 10

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
#define LIT_OS_WINDOWS
#elif __APPLE__
#define LIT_OS_MAC
#elif __linux__
#define LIT_OS_LINUX
#else
#define LIT_OS_UNKNOWN
#endif

#endif
