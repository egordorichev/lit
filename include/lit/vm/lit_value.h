#ifndef LIT_VALUE_H
#define LIT_VALUE_H

#include "lit_common.h"
#include "lit_predefines.h"
#include "util/lit_array.h"

#include <memory.h>

#define SIGN_BIT ((uint64_t) 1 << 63u)
#define QNAN ((uint64_t) 0x7ffc000000000000u)

#define TAG_NULL 1u
#define TAG_FALSE 2u
#define TAG_TRUE 3u

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

static inline double lit_value_to_number(LitValue value) {
	return *((double*) &value);
}

static inline LitValue lit_number_to_value(double num) {
	return *((LitValue*) &num);
}

static inline bool lit_is_falsey(LitValue value) {
	return (IS_BOOL(value) && value == FALSE_VALUE) || IS_NULL(value) || (IS_NUMBER(value) && AS_NUMBER(value) == 0);
}

void lit_print_value(LitValue value);

DECLARE_ARRAY(LitValues, LitValue, values)
void lit_values_ensure_size(LitState* state, LitValues* values, uint size);
void lit_values_ensure_size_empty(LitState* state, LitValues* values, uint size);
const char* lit_get_value_type(LitValue value);

#endif