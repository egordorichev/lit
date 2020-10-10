#include "lit_config.h"
#include "vm/lit_vm.h"
#include "vm/lit_chunk.h"

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

	if (state->vm != NULL) {
		lit_table_delete(&state->vm->modules->values, CONST_STRING(state, "web"));
	}

	lit_interpret(state, "web", source);
}

int main(int argc, char* argv[]) {
	/*
	 * This glue code is used to run lit in the web.
	 * But nothing stops you from taking this code, and using it in your applications:
	 * 
	 * create_state();
	 * interpret("print(32)");
	 * free_state();
	 * 
	 * See src/main.c for more complex use cases
	 */

	return 0;
}