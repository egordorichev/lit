#ifndef LIT_COMMON_H
#define LIT_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>

#include "lit/util/lit_color.h"

#define UNREACHABLE fprintf(stderr, "Unreachable code was reached at %s:%i\n", __FILE__, __LINE__); assert(false);
#define NOT_IMPLEMENTED fprintf(stderr, "Unimplemented code was reached at %s:%i\n", __FILE__, __LINE__); assert(false);
#define UINT8_COUNT UINT8_MAX + 1
#define UINT16_COUNT UINT16_MAX + 1

typedef uint32_t uint;

#define SET_BIT(number, n) number |= 1UL << n;
#define IS_BIT_SET(number, n) (((number >> n) & 1U) != 0)

#endif