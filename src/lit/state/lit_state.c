#include <lit/state/lit_state.h>
#include <stdlib.h>
#include <lit/vm/lit_vm.h>

LitState* lit_new_state() {
	LitState* state = (LitState*) malloc(sizeof(LitState));

	state->vm = (LitVm*) malloc(sizeof(LitState));
	lit_init_vm(state->vm);

	state->bytes_allocated = 0;

	return state;
}

void lit_free_state(LitState* state) {
	lit_free_vm(state->vm);

	free(state->vm);
	free(state);
}