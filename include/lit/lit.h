#ifndef LIT_H
#define LIT_H

#define LIT_VERSION_MAJOR 0
#define LIT_VERSION_MINOR 1
#define LIT_VERSION_STRING "0.1"

#ifndef TEST
// #define LIT_TRACE_EXECUTION
// #define LIT_TRACE_STACK
// #define LIT_TRACE_CHUNK
#define LIT_STRESS_TEST_GC
#define LIT_LOG_GC
#endif

#define LIT_GC_HEAP_GROW_FACTOR 2
#define LIT_CALL_FRAMES_MAX 64
#define LIT_STACK_MAX (LIT_CALL_FRAMES_MAX * 32)
#define LIT_ROOT_MAX 5

#endif