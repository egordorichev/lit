#include <lit/state/lit_state.h>
#include <stdlib.h>

LitState* lit_new_state() {
	LitState* state = (LitState*) malloc(sizeof(LitState));

	state->bytes_allocated = 0;

	return state;
}

void lit_free_state(LitState* state) {
	free(state);
}