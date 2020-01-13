#ifndef LIT_VALUE_H
#define LIT_VALUE_H

#include <lit/lit_common.h>
#include <lit/state/lit_state.h>

typedef double LitValue;

typedef struct {
	uint count;
	uint capacity;

	LitValue* values;
} LitValueArray;

void lit_init_value_array(LitValueArray* chunk);
void lit_free_value_array(LitState* state, LitValueArray* chunk);
void lit_write_value_array(LitState* state, LitValueArray* chunk, LitValue value);
void lit_shrink_value_array(LitState* state, LitValueArray* chunk);

void lit_print_value(LitValue value);

#endif