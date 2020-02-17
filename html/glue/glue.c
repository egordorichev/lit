#include <lit/lit.h>
#include <lit/vm/lit_vm.h>
#include <lit/vm/lit_chunk.h>

#include <stdio.h>

static LitState* state = NULL;

void create_state() {
	if (state != NULL) {
		return;
	}

	state = lit_new_state();
}

void free_state() {
	if (state == NULL) {
		return;
	}

	lit_free_state(state);
	state = NULL;
}

void interpret(const char* source) {
	if (state == NULL) {
		return;
	}

	lit_interpret(state, "web", source);
}

int main(int argc, char* argv[]) {
	return 0;
}