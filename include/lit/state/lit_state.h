#ifndef LIT_STATE_H
#define LIT_STATE_H

#include <lit/lit_common.h>

typedef struct {
	uint bytes_allocated;
} LitState;

LitState* lit_new_state();
void lit_free_state(LitState* state);

#endif