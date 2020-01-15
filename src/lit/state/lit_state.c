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

LitInterpretResult lit_interpret(LitState* state, char* code) {
	LitChunk chunk;
	lit_init_chunk(&chunk);

	lit_setup_scanner(&state->scanner, code);

	LitInterpretResult result = lit_interpret_chunk(state, &chunk);
	lit_free_chunk(state, &chunk);

	return result;
}