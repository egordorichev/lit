#ifndef LIT_COMMON_H
#define LIT_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <assert.h>

#include <lit/util/lit_color.h>

#define UNREACHABLE assert(false);
#define UINT8_COUNT UINT8_MAX + 1
#define UINT16_COUNT UINT16_MAX + 1

typedef uint32_t uint;

#endif