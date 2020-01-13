#include <lit/vm/lit_value.h>
#include <lit/mem/lit_mem.h>

#include <stdio.h>

void lit_init_value_array(LitValueArray* chunk) {
	chunk->count = 0;
	chunk->capacity = 0;

	chunk->values = NULL;
}

void lit_free_value_array(LitState* state, LitValueArray* chunk) {
	LIT_FREE_ARRAY(state, LitValue, chunk->values, chunk->capacity);
	lit_init_value_array(chunk);
}

void lit_write_value_array(LitState* state, LitValueArray* chunk, LitValue value) {
	if (chunk->capacity < chunk->count + 1) {
		uint old_capacity = chunk->capacity;

		chunk->capacity = LIT_GROW_CAPACITY(old_capacity);
		chunk->values = LIT_GROW_ARRAY(state, chunk->values, LitValue, old_capacity, chunk->capacity);
	}

	chunk->values[chunk->count] = value;
	chunk->count++;
}

void lit_shrink_value_array(LitState* state, LitValueArray* chunk) {
	if (chunk->capacity > chunk->count) {
		uint old_capacity = chunk->capacity;

		chunk->capacity = chunk->count;
		chunk->values = LIT_GROW_ARRAY(state, chunk->values, LitValue, old_capacity, chunk->capacity);
	}
}

void lit_print_value(LitValue value) {
	printf("%lf", value);
}