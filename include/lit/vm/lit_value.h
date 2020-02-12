#ifndef LIT_VALUE_H
#define LIT_VALUE_H

#include <lit/lit_common.h>
#include <lit/state/lit_state.h>
#include <lit/util/lit_array.h>

typedef uint64_t LitValue;

#define SIGN_BIT ((uint64_t) 1 << 63)
#define QNAN ((uint64_t) 0x7ffc000000000000)

#define TAG_NULL 1
#define TAG_FALSE 2
#define TAG_TRUE 3

#define IS_BOOL(v) (((v) & FALSE_VALUE) == FALSE_VALUE)
#define IS_NULL(v) ((v) == NULL_VALUE)
#define IS_NUMBER(v) (((v) & QNAN) != QNAN)
#define IS_OBJECT(v) (((v) & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT))

#define AS_BOOL(v) ((v) == TRUE_VALUE)
#define AS_NUMBER(v) lit_value_to_number(v)
#define AS_OBJECT(v) ((LitObject*) (uintptr_t) ((v) & ~(SIGN_BIT | QNAN)))

#define BOOL_VALUE(boolean) ((boolean) ? TRUE_VALUE : FALSE_VALUE)
#define FALSE_VALUE ((LitValue) (uint64_t) (QNAN | TAG_FALSE))
#define TRUE_VALUE ((LitValue) (uint64_t) (QNAN | TAG_TRUE))
#define NULL_VALUE ((LitValue) (uint64_t) (QNAN | TAG_NULL))
#define NUMBER_VALUE(num) lit_number_to_value(num)

#define OBJECT_VALUE(obj) (LitValue) (SIGN_BIT | QNAN | (uint64_t) (uintptr_t) (obj))

typedef union {
	uint64_t bits64;
	uint32_t bits32[2];
	double num;
} LitDoubleUnion;

static inline double lit_value_to_number(LitValue value) {
	LitDoubleUnion data;
	data.bits64 = value;
	return data.num;
}

static inline LitValue lit_number_to_value(double num) {
	LitDoubleUnion data;
	data.num = num;
	return data.bits64;
}

DECLARE_ARRAY(LitValues, LitValue, values)
void lit_print_value(LitValue value);

#endif