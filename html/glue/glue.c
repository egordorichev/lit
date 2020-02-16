#include <lit/lit.h>
#include <lit/vm/lit_vm.h>
#include <lit/vm/lit_chunk.h>

#include <stdio.h>

int main(int argc, char* argv[]) {
	LitState* state = lit_new_state();
	lit_interpret(state, "web", "print(\"Halla world\")");
	lit_free_state(state);

	return 0;
}