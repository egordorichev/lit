#ifndef LIT_ARRAY_H
#define LIT_ARRAY_H

#include "lit_common.h"
#include "lit_predefines.h"

#define DECLARE_ARRAY(name, type, shr) \
	typedef struct { \
		uint capacity; \
		uint count; \
		type* values; \
	} name; \
	void lit_init_##shr(name* array); \
	void lit_free_##shr(LitState* state, name* array); \
	void lit_##shr##_write(LitState* state, name* array, type value);

#define DEFINE_ARRAY(name, type, shr) \
	void lit_init_##shr(name* array) { \
		array->values = NULL; \
		array->capacity = 0; \
		array->count = 0; \
	} \
	\
	void lit_free_##shr(LitState* state, name* array) { \
		LIT_FREE_ARRAY(state, type, array->values, array->capacity); \
		lit_init_##shr(array); \
	} \
	\
	void lit_##shr##_write(LitState* state, name* array, type value) { \
		if (array->capacity < array->count + 1) { \
			uint old_capacity = array->capacity; \
			array->capacity = LIT_GROW_CAPACITY(old_capacity); \
			array->values = LIT_GROW_ARRAY(state, array->values, type, old_capacity, array->capacity); \
		} \
		\
		array->values[array->count] = value; \
		array->count++; \
	}

DECLARE_ARRAY(LitUInts, uint, uints)
DECLARE_ARRAY(LitBytes, uint8_t, bytes)

#endif